/******************************************************************************
 * Copyright 2019 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/drivers/video/tools/decode_video/frame_processor.h"

#include <fstream>
#include <memory>
#include <sstream>

#include "cyber/common/log.h"
#include "modules/common/util/string_util.h"
#include "modules/drivers/video/tools/decode_video/h265_decoder.h"

namespace apollo {
namespace drivers {
namespace video {

using apollo::common::util::StrCat;

FrameProcessor::FrameProcessor(const std::string& input_video_file,
                               const std::string& output_jpg_dir)
    : output_jpg_dir_(output_jpg_dir) {
  std::ifstream video_file(input_video_file, std::ios::binary);
  std::vector<uint8_t> video_buffer(
      (std::istreambuf_iterator<char>(video_file)),
      std::istreambuf_iterator<char>());
  input_video_buffer_ = video_buffer;
}

bool FrameProcessor::ProcessStream() const {
  if (input_video_buffer_.empty()) {
    AERROR << "error: failed to read from input video file";
    return false;
  }
  AVCodecParserContext* codec_parser = av_parser_init(AV_CODEC_ID_H265);
  if (codec_parser == nullptr) {
    AERROR << "error: failed to init AVCodecParserContext";
    return false;
  }
  AVPacket apt;
  av_init_packet(&apt);
  const std::unique_ptr<H265Decoder> decoder(new H265Decoder());
  if (!decoder->Init()) {
    AERROR << "error: failed to init decoder";
    return false;
  }
  uint32_t local_size = static_cast<uint32_t>(input_video_buffer_.size());
  uint8_t* local_data = const_cast<uint8_t*>(input_video_buffer_.data());
  AINFO << "decoding: video size = " << local_size;
  int frame_num = 0;
  int error_frame_num = 0;
  while (local_size > 0) {
    int frame_len = av_parser_parse2(
        codec_parser, decoder->GetCodecCtxH265(), &(apt.data), &(apt.size),
        local_data, local_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
    if (apt.data == nullptr) {
      apt.data = local_data;
      apt.size = local_size;
    }
    AINFO << "frame " << frame_num << ": frame_len=" << frame_len
          << ". left_size=" << local_size;
    std::vector<uint8_t> jpeg_buffer = decoder->Process(apt.data, apt.size);
    if (!jpeg_buffer.empty()) {
      WriteOutputJpgFile(jpeg_buffer, GetOutputFile(frame_num));
      frame_num++;
    } else {
      error_frame_num++;
    }
    local_data += frame_len;
    local_size -= frame_len;
  }
  // Trying to decode the left over frames from buffer
  for (int i = error_frame_num; i >= 0; i--) {
    std::vector<uint8_t> jpeg_buffer = decoder->Process(nullptr, 0);
    if (!jpeg_buffer.empty()) {
      WriteOutputJpgFile(jpeg_buffer, GetOutputFile(frame_num));
      AINFO << "frame " << frame_num << ": read from buffer";
      frame_num++;
    }
  }
  AINFO << "total frames: " << frame_num;
  av_parser_close(codec_parser);
  return true;
}

std::string FrameProcessor::GetOutputFile(const int frame_num) const {
  constexpr int kSuffixLen = 5;
  std::stringstream jpg_suffix;
  jpg_suffix.fill('0');
  jpg_suffix.width(kSuffixLen);
  jpg_suffix << std::to_string(frame_num);
  return StrCat(output_jpg_dir_, "/", jpg_suffix.str(), ".jpg");
}

void FrameProcessor::WriteOutputJpgFile(
    const std::vector<uint8_t>& jpeg_buffer,
    const std::string& output_jpg_file) const {
  std::ofstream out(output_jpg_file, std::ios::binary);
  for (const uint8_t current : jpeg_buffer) {
    out << static_cast<char>(current);
  }
}

}  // namespace video
}  // namespace drivers
}  // namespace apollo