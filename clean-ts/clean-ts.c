/*
 * Copyright (c) 2014 Kohei Suzuki
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <getopt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

static const int TS_PACKET_SIZE = 188;

#define FAIL_IF_ERROR(s) err = (s); if (err < 0) goto fail

#ifdef CLEAN_TS_DEBUG
# define DPRINTF(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
# define DPRINTF(fmt, ...)
#endif

static int find_main_streams(const AVFormatContext *ic, AVStream **input_streams, size_t input_stream_size, size_t *num_found_ptr)
{
  /* Select audio and video in the most small program id */
  static const int INVALID_PROGRAM_ID = 1000000000;
  int program_id = INVALID_PROGRAM_ID;
  unsigned i;

  *num_found_ptr = 0;
  DPRINTF("nb_programs = %d\n", ic->nb_programs);
  for (i = 0; i < ic->nb_programs; i++) {
    AVProgram *program = ic->programs[i];
    size_t num_found = 0;
    unsigned j;
    int audio_found = 0, video_found = 0;

    if (program_id < program->id) {
      continue;
    }
    for (j = 0; j < program->nb_stream_indexes; j++) {
      AVStream *stream = ic->streams[program->stream_index[j]];
      const enum AVMediaType media_type = stream->codecpar->codec_type;
      switch (media_type) {
        case AVMEDIA_TYPE_AUDIO:
        case AVMEDIA_TYPE_VIDEO:
          DPRINTF("programs[%d]: %s %u [0x%x] duration=%" PRId64 "\n",
              program->id,
              media_type == AVMEDIA_TYPE_AUDIO ? "audio" : "video",
              stream->index,
              stream->id,
              stream->duration);
          if (stream->duration > 0LL || stream->duration == AV_NOPTS_VALUE) {
            if (num_found >= input_stream_size) {
              fprintf(stderr, "Too many streams found: %zu\n", num_found);
              return AVERROR_STREAM_NOT_FOUND;
            }
            input_streams[num_found++] = stream;
            if (media_type == AVMEDIA_TYPE_AUDIO) {
              audio_found = 1;
            } else {
              video_found = 1;
            }
          }
          break;
        default:
          break;
      }
    }
    if (audio_found && video_found) {
      program_id = program->id;
      *num_found_ptr = num_found;
    }
  }
  if (program_id == INVALID_PROGRAM_ID) {
    return AVERROR_STREAM_NOT_FOUND;
  } else {
    return 0;
  }
}

static int clean_ts(const char *infile, const char *outfile, int64_t npackets,
                    int log_level) {
  AVFormatContext *ic = NULL, *oc = NULL;
  int err = 0;
  AVStream **output_streams = NULL;

  FAIL_IF_ERROR(avformat_open_input(&ic, infile, NULL, NULL));
  avio_seek(ic->pb, npackets * TS_PACKET_SIZE, SEEK_SET);
  FAIL_IF_ERROR(avformat_find_stream_info(ic, NULL));

  av_log_set_level(log_level);

  AVStream *input_streams[8] = { NULL };
  size_t input_stream_size;
  FAIL_IF_ERROR(find_main_streams(ic, input_streams, sizeof(input_streams)/sizeof(input_streams[0]), &input_stream_size));
  DPRINTF("%zu streams found\n", input_stream_size);

  FAIL_IF_ERROR(avformat_alloc_output_context2(&oc, NULL, NULL, outfile));
  output_streams = av_mallocz_array(input_stream_size, sizeof(*output_streams));
  size_t i;
  for (i = 0; i < input_stream_size; i++) {
    const AVCodec *codec = avcodec_find_encoder(input_streams[i]->codecpar->codec_id);
    output_streams[i] = avformat_new_stream(oc, codec);
    DPRINTF("%d: Copy from [0x%x]\n", output_streams[i]->index, input_streams[i]->index);
    FAIL_IF_ERROR(avcodec_parameters_copy(output_streams[i]->codecpar,
                                          input_streams[i]->codecpar));
    output_streams[i]->time_base = input_streams[i]->time_base;
  }

  if (!(oc->oformat->flags & AVFMT_NOFILE)) {
    FAIL_IF_ERROR(avio_open(&oc->pb, outfile, AVIO_FLAG_WRITE));
  }

  FAIL_IF_ERROR(avformat_write_header(oc, NULL));
  AVPacket packet;
  int error_count = 0;
  while ((err = av_read_frame(ic, &packet)) >= 0) {
    const AVStream *in_stream = ic->streams[packet.stream_index];
    AVStream *out_stream = NULL;
    for (i = 0; i < input_stream_size; i++) {
      if (in_stream == input_streams[i]) {
        out_stream = output_streams[i];
      }
    }
    if (out_stream != NULL) {
      packet.stream_index = out_stream->index;
      packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
      packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
      packet.duration = av_rescale_q_rnd(packet.duration, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
      packet.pos = -1;
      err = av_interleaved_write_frame(oc, &packet);
      if (err < 0) {
        fprintf(stderr, "av_interleaved_write_frame(): %s (at %"PRId64")\n", av_err2str(err), avio_tell(ic->pb));
        ++error_count;
      }
    }
    av_packet_unref(&packet);
    if (error_count >= 10) {
      goto fail;
    }
  }
  if (err != AVERROR_EOF) {
    goto fail;
  }
  FAIL_IF_ERROR(av_write_trailer(oc));

fail:
  avformat_close_input(&ic);
  if (oc != NULL && oc->pb != NULL) {
    avio_close(oc->pb);
  }
  avformat_free_context(oc);
  av_free(output_streams);
  return err;
}

static const int HD = 0x01, SD = 0x02;
static const int FULL_HD_WIDTH = 1920, HD_WIDTH = 1440, SD_WIDTH = 720;

static void detect_stream_status(const char *infile, int64_t npackets, int *hd_or_sd, int *valid_sample_fmt)
{
  AVFormatContext *ic = NULL;
  int err = 0;
  unsigned i;

  *hd_or_sd = 0;
  *valid_sample_fmt = -1;
  FAIL_IF_ERROR(avformat_open_input(&ic, infile, NULL, NULL));
  avio_seek(ic->pb, npackets * TS_PACKET_SIZE, SEEK_SET);
  FAIL_IF_ERROR(avformat_find_stream_info(ic, NULL));

  for (i = 0; i < ic->nb_streams; i++) {
    const AVCodecParameters *params = ic->streams[i]->codecpar;
    switch (params->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
        if (params->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
          if (params->width == HD_WIDTH || params->width == FULL_HD_WIDTH) {
            *hd_or_sd |= HD;
          } else if (params->width == SD_WIDTH) {
            *hd_or_sd |= SD;
          }
        }
        break;
      case AVMEDIA_TYPE_AUDIO:
        if (params->format == AV_SAMPLE_FMT_NONE || params->sample_rate == 0) {
          *valid_sample_fmt = 0;
        } else {
          if (*valid_sample_fmt == -1) {
            *valid_sample_fmt = 1;
          }
        }
        break;
      default:
        break;
    }
  }

fail:
  avformat_close_input(&ic);
}

static int has_stray_audio(const char *infile, int64_t npackets)
{
  AVFormatContext *ic = NULL;
  int err = 0;
  int ret = 0;
  uint8_t *found_streams = NULL;

  FAIL_IF_ERROR(avformat_open_input(&ic, infile, NULL, NULL));
  avio_seek(ic->pb, npackets * TS_PACKET_SIZE, SEEK_SET);
  FAIL_IF_ERROR(avformat_find_stream_info(ic, NULL));

  /* When an audio stream is found outside the programs, ffmpeg seems to fail
   * with the following error message.
   *
   * [mpegts @ 0x???????] AAC bitstream not in ADTS format and extradata missing
   * av_interleaved_write_frame(): Invalid data found when processing input
   *
   * We have to avoid this kind of error.
   */

  found_streams = av_mallocz(ic->nb_streams);
  unsigned i;
  for (i = 0; i < ic->nb_programs; i++) {
    const AVProgram *program = ic->programs[i];
    unsigned j;
    for (j = 0; j < program->nb_stream_indexes; j++) {
      found_streams[program->stream_index[j]] = 1;
    }
  }

  for (i = 0; i < ic->nb_streams; i++) {
    if (!found_streams[i] && ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      ret = 1;
      break;
    }
  }

fail:
  avformat_close_input(&ic);
  av_free(found_streams);
  return ret;
}

static int higher_p(const char *infile, int64_t npackets, int higher_is_hd)
{
  if (has_stray_audio(infile, npackets)) {
    fprintf(stderr, "%s: Stray audio is found at %"PRId64"*188\n", infile, npackets);
    return 1;
  } else {
    int hd_or_sd, valid_sample_fmt;
    detect_stream_status(infile, npackets, &hd_or_sd, &valid_sample_fmt);
    if (!valid_sample_fmt) {
      DPRINTF("invalid sample_fmt at %" PRId64 "\n", npackets);
      return 1;
    } else if ((hd_or_sd & HD) && (hd_or_sd & SD)) {
      // If both are found, proper cutpoint is higher.
      return 1;
    } else if (hd_or_sd & HD) {
      return !higher_is_hd;
    } else if (hd_or_sd & SD) {
      return higher_is_hd;
    } else {
      fprintf(stderr, "%s: Neither HD nor SD at %"PRId64"\n", infile, npackets);
      return 1;
    }
  }
}

static int64_t find_cutpoint(const char *infile, int64_t lo, int64_t hi, int higher_is_hd)
{
  while (lo < hi) {
    DPRINTF("%" PRId64 " - %" PRId64 "\n", lo, hi);
    const int64_t mid = (lo + hi) / 2;
    const int r = higher_p(infile, mid, higher_is_hd);
    if (r < 0) {
      return r;
    } else if (r) {
      lo = mid+1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static int count_audio_streams(const char *infile, int64_t npackets) {
  AVFormatContext *ic = NULL;
  AVCodecContext **contexts = NULL;
  AVFrame **initial_frames = NULL;
  int err = 0;
  int audio_count = 0;
  int is_valid = 1;
  unsigned i;

  FAIL_IF_ERROR(avformat_open_input(&ic, infile, NULL, NULL));
  avio_seek(ic->pb, npackets * TS_PACKET_SIZE, SEEK_SET);
  FAIL_IF_ERROR(avformat_find_stream_info(ic, NULL));

  for (i = 0; i < ic->nb_streams; i++) {
    const AVStream *stream = ic->streams[i];
    const AVCodecParameters *params = stream->codecpar;
    switch (params->codec_type) {
      case AVMEDIA_TYPE_AUDIO:
        if ((stream->duration > 0LL || stream->duration == AV_NOPTS_VALUE) &&
            params->format != AV_SAMPLE_FMT_NONE && params->sample_rate != 0) {
          ++audio_count;
        }
        break;
      default:
        break;
    }
  }

  contexts = av_mallocz_array(audio_count, sizeof(*contexts));
  initial_frames = av_mallocz_array(audio_count, sizeof(*initial_frames));

  for (i = 0; i < ic->nb_streams; i++) {
    const AVStream *stream = ic->streams[i];
    const AVCodecParameters *params = stream->codecpar;
    if (params->codec_type == AVMEDIA_TYPE_AUDIO) {
      AVCodec *codec = avcodec_find_decoder(params->codec_id);
      AVCodecContext *avctx = avcodec_alloc_context3(codec);
      avcodec_parameters_to_context(avctx, params);
      avcodec_open2(avctx, codec, NULL);
      contexts[stream->index] = avctx;
      initial_frames[stream->index] = av_frame_alloc();
    }
  }

  AVPacket packet;
  int checked_audio_count = 0;
  while (av_read_frame(ic, &packet) >= 0 && checked_audio_count < audio_count) {
    AVCodecContext *avctx = contexts[packet.stream_index];
    if (avctx != NULL) {
      AVFrame *frame = initial_frames[packet.stream_index];
      if ((err = avcodec_send_packet(contexts[packet.stream_index], &packet)) != 0) {
        // This cutpoint is invalid.
        DPRINTF("stream %d is invalid because avcodec_send_packet failed: %s\n", packet.stream_index, av_err2str(err));
        ++checked_audio_count;
        is_valid = 0;
        avcodec_free_context(&avctx);
        contexts[packet.stream_index] = NULL;
        av_frame_unref(frame);
        initial_frames[packet.stream_index] = NULL;
      } else {
        if (avcodec_receive_frame(contexts[packet.stream_index], initial_frames[packet.stream_index]) != 0) {
          // continue
          DPRINTF("stream %d: avcodec_receive_frame failed\n", packet.stream_index);
        } else {
          // This cutpoint is valid
          DPRINTF("stream %d is valid\n", packet.stream_index);
          ++checked_audio_count;
          avcodec_free_context(&avctx);
          contexts[packet.stream_index] = NULL;
          av_frame_unref(frame);
          initial_frames[packet.stream_index] = NULL;
        }
      }
    }
    av_packet_unref(&packet);
  }

fail:
  av_free(contexts);
  av_free(initial_frames);
  avformat_close_input(&ic);

  DPRINTF("count_audio_streams: npackets=%" PRId64 ": audio_count=%u is_valid=%d\n",
          npackets, audio_count, is_valid);
  if (err != 0 || !is_valid || checked_audio_count != audio_count) {
    return -1;
  } else {
    return audio_count;
  }
}

static int64_t find_multi_audio_cutpoint(const char *infile, int64_t lo, int64_t hi) {
  int lo_count, hi_count;

  lo_count = count_audio_streams(infile, lo);
  hi_count = count_audio_streams(infile, hi);
  if (lo_count == hi_count) {
    return lo;
  }

  while (lo < hi) {
    DPRINTF("find_multi_audio_cutpoint: %" PRId64 " - %" PRId64 "\n", lo, hi);
    const int64_t mid = (lo + hi) / 2;
    const int c = count_audio_streams(infile, mid);
    if (c == -1 || c == lo_count) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  DPRINTF("find_multi_audio_cutpoint: result=%" PRId64 "\n", lo);
  return lo;
}

int main(int argc, char *argv[])
{
  static const struct option long_options[] = {
    { "retry", no_argument, 0, 0 },
    { 0, 0, 0, 0 },
  };
  int option_index = 0;
  int enable_retry = 0;
  while (getopt_long(argc, argv, "", long_options, &option_index) == 0) {
    switch (option_index) {
      case 0:
        enable_retry = 1;
        break;
    }
  }
  if (optind + 2 != argc) {
    fprintf(stderr, "Usage: %s [--retry] input.ts output.ts\n", argv[0]);
    return 1;
  }

  const char *infile = argv[optind], *outfile = argv[optind+1];
  av_register_all();
  av_log_set_level(AV_LOG_FATAL);

  static const int MAX_PACKETS = 200000;
  int begin_hd_or_sd, end_hd_or_sd;
  int begin_valid_sample_fmt, end_valid_sample_fmt;
  detect_stream_status(infile, 0, &begin_hd_or_sd, &begin_valid_sample_fmt);
  detect_stream_status(infile, MAX_PACKETS, &end_hd_or_sd, &end_valid_sample_fmt);
  const int begin_hd = begin_hd_or_sd & HD;
  const int end_hd = end_hd_or_sd & HD;
  int err;
  DPRINTF("begin: hd_or_sd:%d valid_sample_fmt:%d\n", begin_hd_or_sd, begin_valid_sample_fmt);
  DPRINTF("end:   hd_or_sd:%d valid_sample_fmt:%d\n", end_hd_or_sd, end_valid_sample_fmt);
  int64_t npackets = 0;

  if (begin_hd) {
    if (end_hd) {
      if (!begin_valid_sample_fmt) {
        npackets = find_cutpoint(infile, 0, MAX_PACKETS, 1);
        DPRINTF("cutpoint for valid sample_fmt with HD: %" PRId64 "\n", npackets);
      }
    } else {
      npackets = find_cutpoint(infile, 0, MAX_PACKETS, 0);
      DPRINTF("cutpoint for HD -> SD: %" PRId64 "\n", npackets);
    }
  } else {
    if (end_hd) {
      npackets = find_cutpoint(infile, 0, MAX_PACKETS, 1);
      DPRINTF("cutpoint for SD -> HD: %" PRId64 "\n", npackets);
    } else {
      if (!begin_valid_sample_fmt) {
        npackets = find_cutpoint(infile, 0, MAX_PACKETS, 0);
        DPRINTF("cutpoint for valid sample_fmt with SD: %" PRId64 "\n", npackets);
      }
    }
  }

  npackets = find_multi_audio_cutpoint(infile, npackets, MAX_PACKETS);

  if (npackets < 0) {
    err = npackets;
  } else {
    err = clean_ts(infile, outfile, npackets, AV_LOG_ERROR);
    if (err == -EINVAL && enable_retry) {
      DPRINTF("Retry clean_ts by binary search\n");
      int lo = npackets, hi = MAX_PACKETS;
      while (lo < hi) {
        const int mid = (lo + hi) / 2;
        DPRINTF("  Try npackets=%d\n", mid);
        err = clean_ts(infile, outfile, mid, AV_LOG_FATAL);
        if (err == -EINVAL) {
          DPRINTF("    Failed\n");
          lo = mid+1;
        } else if (err == 0) {
          DPRINTF("    Succeeded\n");
          hi = mid;
        } else {
          DPRINTF("    Error\n");
          break;
        }
      }
      DPRINTF("Determined %d\n", lo);
      err = clean_ts(infile, outfile, lo, AV_LOG_ERROR);
    }
  }

  if (err < 0) {
    fprintf(stderr, "ERROR: %s\n", av_err2str(err));
  }
  return -err;
}
