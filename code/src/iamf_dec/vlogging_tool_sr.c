/*
BSD 3-Clause Clear License The Clear BSD License

Copyright (c) 2023, Alliance for Open Media.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * @file vlogging_tool_sr.h
 * @brief verification log generator.
 * @version 0.1
 * @date Created 03/29/2023
 **/

#if defined(__linux__)
#include <unistd.h>
#else
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IAMF_OBU.h"
#include "bitstreamrw.h"
#include "vlogging_tool_sr.h"

#define LOG_BUFFER_SIZE 100000

typedef struct vlogdata {
  int log_type;
  uint64_t key;
  struct vlogdata* prev;
  struct vlogdata* next;
  int is_longtext;
  char* ltext;
  char text[256];
} VLOG_DATA;

typedef struct vlog_file {
  FILE* f;
  char file_name[FILENAME_MAX];
  int is_open;
  VLOG_DATA* head[MAX_LOG_TYPE];
} VLOG_FILE;

static VLOG_FILE log_file = {
    0,
};

static uint32_t get_4cc_codec_id(char a, char b, char c, char d) {
  return ((a) | (b << 8) | (c << 16) | (d << 24));
}

static uint32_t swapByteOrder(uint32_t x) {
  return (x >> 24) | ((x >> 8) & 0x0000FF00) | ((x << 8) & 0x00FF0000) |
         (x << 24);
}

int vlog_file_open(const char* log_file_name) {
  int i;
  if (log_file.is_open && strcmp(log_file_name, log_file.file_name) == 0) {
    return 0;
  }

#if defined(__linux__)
  if (access(log_file_name, 0) != -1) {
#else
  if (_access(log_file_name, 0) != -1) {
#endif
    // already exist
    if (remove(log_file_name) == -1) return -1;
  }

  {
    log_file.f = fopen(log_file_name, "a");
    strcpy(log_file.file_name, log_file_name);
    log_file.is_open = 1;

    for (i = 0; i < MAX_LOG_TYPE; i++) {
      log_file.head[i] = NULL;
    }

    return 0;
  }
}

int vlog_file_close() {
  int i;
  VLOG_DATA *t_logdata, *t_logdata_free;
  int print_order[MAX_LOG_TYPE] = {LOG_MP4BOX, LOG_OBU, LOG_DECOP};

  if (log_file.f && log_file.is_open) {
    for (i = 0; i < MAX_LOG_TYPE; i++) {
      t_logdata = log_file.head[print_order[i]];
      while (t_logdata) {
        if (t_logdata->is_longtext) {
          fprintf(log_file.f, "%s", t_logdata->ltext);
          free(t_logdata->ltext);
          t_logdata_free = t_logdata;
          t_logdata = t_logdata->next;
          free(t_logdata_free);
        } else {
          fprintf(log_file.f, "%s", t_logdata->text);
          t_logdata_free = t_logdata;
          t_logdata = t_logdata->next;
          free(t_logdata_free);
        }
      }
      log_file.head[print_order[i]] = NULL;
    }

    fclose(log_file.f);
    log_file.f = NULL;
    memset(log_file.file_name, 0, sizeof(log_file.file_name));
    log_file.is_open = 0;

    return 0;
  }

  return -1;
}

int is_vlog_file_open() {
  if (!log_file.f || !log_file.is_open) return 0;
  return (1);
}

int vlog_print(LOG_TYPE type, uint64_t key, const char* format, ...) {
  if (!log_file.f || !log_file.is_open) return -1;

  va_list args;
  int len = 0;
  char* buffer = NULL;
  VLOG_DATA *t_logdata, *logdata_new;

  va_start(args, format);
#if defined(__linux__)
  len = vprintf(format, args) + 1;
#else
  len = _vscprintf(format, args) + 1;  // terminateing '\0'
#endif

  buffer = (char*)malloc(len * sizeof(char));
  if (buffer) {
#if defined(__linux__)
    vsnprintf(buffer, len, format, args);
#else
    vsprintf_s(buffer, len, format, args);
#endif
  }
  va_end(args);

  logdata_new = malloc(sizeof(VLOG_DATA));
  if (logdata_new) {
    // set up new logdata
    logdata_new->log_type = type;
    logdata_new->key = key;
    if (len < sizeof(logdata_new->text)) {  // short text log
      strcpy(logdata_new->text, buffer);
      free(buffer);
      logdata_new->is_longtext = 0;
      logdata_new->ltext = NULL;
    } else {  // longtext log
      logdata_new->is_longtext = 1;
      logdata_new->ltext = buffer;
      logdata_new->text[0] = 0;
    }

    // add new logdata into logdata chain
    t_logdata = log_file.head[type];
    if (log_file.head[type] == NULL) {  // head is empty
      logdata_new->prev = NULL;
      logdata_new->next = NULL;
      log_file.head[type] = logdata_new;
    } else {  // head is ocuppied.
      if (logdata_new->key <
          t_logdata->key)  // t_logdata == log_file.head[log_type];
      {                    // head is needed to update
        logdata_new->next = t_logdata->next;
        t_logdata->prev = logdata_new;
        log_file.head[type] = logdata_new;
      } else {  // body or tail is needed to update
        while (t_logdata) {
          if (logdata_new->key < t_logdata->key) {  // add ne logdata into body
            logdata_new->next = t_logdata;
            t_logdata->prev->next = logdata_new;
            logdata_new->prev = t_logdata->prev;
            t_logdata->prev = logdata_new;
            break;
          } else {
            if (t_logdata->next) {  // skip this t_logdata
              t_logdata = t_logdata->next;
            } else {  // add new logdata into tail
              logdata_new->prev = t_logdata;
              t_logdata->next = logdata_new;
              logdata_new->next = NULL;
              break;
            }
          }
        }
      }
    }
  }

  return 0;
}

int write_prefix(LOG_TYPE type, char* buf) {
  int len = 0;

  switch (type) {
    case LOG_OBU:
      len = sprintf(buf, "#0\n");
      break;
    case LOG_MP4BOX:
      len = sprintf(buf, "#1\n");
      break;
    case LOG_DECOP:
      len = sprintf(buf, "$0\n");
      break;
    default:
      break;
  }

  return len;
}

int write_postfix(LOG_TYPE type, char* buf) {
  int len = 0;

  switch (type) {
    case LOG_OBU:
    case LOG_MP4BOX:
      len = sprintf(buf, "##\n");
      break;
    case LOG_DECOP:
      len = sprintf(buf, "$$\n");
      break;
    default:
      break;
  }

  return len;
}

int write_yaml_form(char* log, uint8_t indent, const char* format, ...) {
  int ret = 0;
  for (uint8_t i = 0; i < indent; ++i) {
    ret += sprintf(log + ret, "  ");
  }

  va_list args;
  int len = 0;

  va_start(args, format);
#if defined(__linux__)
  len = vprintf(format, args) + 1;
  va_start(args, format);
  vsnprintf(log + ret, len, format, args);
#else
  len = _vscprintf(format, args) + 1;  // terminateing '\0'
  vsprintf_s(log + ret, len, format, args);
#endif
  va_end(args);

  ret += len - 1;
  ret += sprintf(log + ret, "\n");

  return ret;
}

static void write_magic_code_log(uint64_t idx, void* obu, char* log) {
  IAMF_Version* mc_obu = (IAMF_Version*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "MagicCodeOBU_%llu:", idx);
  log += write_yaml_form(log, 0, "- ia_code: %u",
                         swapByteOrder(mc_obu->iamf_code));
  log += write_yaml_form(log, 1, "version: %u", mc_obu->version);
  log +=
      write_yaml_form(log, 1, "profile_version: %u", mc_obu->profile_version);
  write_postfix(LOG_OBU, log);
}

static void write_codec_config_log(uint64_t idx, void* obu, char* log) {
  IAMF_CodecConf* cc_obu = (IAMF_CodecConf*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "CodecConfigOBU_%llu:", idx);
  log +=
      write_yaml_form(log, 0, "- codec_config_id: %llu", cc_obu->codec_conf_id);
  log += write_yaml_form(log, 1, "codec_config:");
  log +=
      write_yaml_form(log, 2, "codec_id: %u", swapByteOrder(cc_obu->codec_id));
  log += write_yaml_form(log, 2, "num_samples_per_frame: %llu",
                         cc_obu->nb_samples_per_frame);
  log += write_yaml_form(log, 2, "roll_distance: %d", cc_obu->roll_distance);

  // NOTE: Self parsing

  if (cc_obu->codec_id == get_4cc_codec_id('m', 'p', '4', 'a') ||
      cc_obu->codec_id == get_4cc_codec_id('e', 's', 'd', 's')) {
    // __KWON_TODO
  } else if (cc_obu->codec_id == get_4cc_codec_id('O', 'p', 'u', 's') ||
             cc_obu->codec_id == get_4cc_codec_id('d', 'O', 'p', 's')) {
    uint8_t version = get_uint8(cc_obu->decoder_conf, 0);
    uint8_t output_channel_count = get_uint8(cc_obu->decoder_conf, 1);
    uint16_t pre_skip = get_uint16be(cc_obu->decoder_conf, 2);
    uint32_t input_sample_rate = get_uint32be(cc_obu->decoder_conf, 4);
    uint16_t output_gain = get_uint16be(cc_obu->decoder_conf, 8);
    uint8_t channel_mapping_family = get_uint8(cc_obu->decoder_conf, 10);

    log += write_yaml_form(log, 2, "decoder_config_opus:");
    log += write_yaml_form(log, 3, "version: %u", version);
    log += write_yaml_form(log, 3, "output_channel_count: %u",
                           output_channel_count);
    log += write_yaml_form(log, 3, "pre_skip: %u", pre_skip);
    log += write_yaml_form(log, 3, "input_sample_rate: %lu", input_sample_rate);
    log += write_yaml_form(log, 3, "output_gain: %u", output_gain);
    log +=
        write_yaml_form(log, 3, "mapping_family: %u", channel_mapping_family);
  } else if (cc_obu->codec_id == get_4cc_codec_id('i', 'p', 'c', 'm')) {
    uint8_t sample_format_flags = get_uint8(cc_obu->decoder_conf, 0);
    uint8_t sample_size = get_uint8(cc_obu->decoder_conf, 1);
    uint32_t sample_rate = get_uint32be(cc_obu->decoder_conf, 2);

    log += write_yaml_form(log, 2, "decoder_config_lpcm:");
    log +=
        write_yaml_form(log, 3, "sample_format_flags: %u", sample_format_flags);
    log += write_yaml_form(log, 3, "sample_size: %u", sample_size);
    log += write_yaml_form(log, 3, "sample_rate: %lu", sample_rate);
  }
  write_postfix(LOG_OBU, log);
}

static void write_audio_element_log(uint64_t idx, void* obu, char* log) {
  IAMF_Element* ae_obu = (IAMF_Element*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "AudioElementOBU_%llu:", idx);
  log +=
      write_yaml_form(log, 0, "- audio_element_id: %llu", ae_obu->element_id);
  log +=
      write_yaml_form(log, 1, "audio_element_type: %u", ae_obu->element_type);
  log +=
      write_yaml_form(log, 1, "codec_config_id: %llu", ae_obu->codec_config_id);
  log += write_yaml_form(log, 1, "num_substreams: %llu", ae_obu->nb_substreams);

  log += write_yaml_form(log, 1, "audio_substream_ids:");
  for (uint64_t i = 0; i < ae_obu->nb_substreams; ++i) {
    log += write_yaml_form(log, 1, "- %llu", ae_obu->substream_ids[i]);
  }
  log += write_yaml_form(log, 1, "num_parameters: %llu", ae_obu->nb_parameters);
  if (ae_obu->element_type == AUDIO_ELEMENT_TYPE_CHANNEL_BASED) {
    log += write_yaml_form(log, 1, "scalable_channel_layout_config:");
    log += write_yaml_form(log, 2, "num_layers: %u",
                           ae_obu->channels_conf->nb_layers);
    log += write_yaml_form(log, 2, "channel_audio_layer_configs:");
    for (uint32_t i = 0; i < ae_obu->channels_conf->nb_layers; ++i) {
      ChannelLayerConf* layer_conf = &ae_obu->channels_conf->layer_conf_s[i];
      log += write_yaml_form(log, 2, "- loudspeaker_layout: %u",
                             layer_conf->loudspeaker_layout);
      log += write_yaml_form(log, 3, "output_gain_is_present_flag: %u",
                             layer_conf->output_gain_flag);
      log += write_yaml_form(log, 3, "recon_gain_is_present_flag: %u",
                             layer_conf->recon_gain_flag);
      log += write_yaml_form(log, 3, "substream_count: %u",
                             layer_conf->nb_substreams);
      log += write_yaml_form(log, 3, "coupled_substream_count: %u",
                             layer_conf->nb_coupled_substreams);
    }
  } else if (ae_obu->element_type == AUDIO_ELEMENT_TYPE_SCENE_BASED) {
    // log += write_yaml_form(log, 0, "- scene_based:");
    log += write_yaml_form(log, 1, "ambisonics_config:");

    AmbisonicsConf* ambisonics_conf = ae_obu->ambisonics_conf;
    log += write_yaml_form(log, 2, "ambisonics_mode: %llu",
                           ambisonics_conf->ambisonics_mode);
    if (ambisonics_conf->ambisonics_mode == AMBISONICS_MONO) {
      log += write_yaml_form(log, 2, "ambisonics_mono_config:");
      log += write_yaml_form(log, 3, "output_channel_count: %u",
                             ambisonics_conf->output_channel_count);
      log += write_yaml_form(log, 3, "substream_count: %u",
                             ambisonics_conf->substream_count);

      log += write_yaml_form(log, 3, "channel_mapping:");
      for (uint64_t i = 0; i < ambisonics_conf->mapping_size; ++i) {
        log += write_yaml_form(log, 3, "- %u", ambisonics_conf->mapping[i]);
      }
    } else if (ambisonics_conf->ambisonics_mode == AMBISONICS_PROJECTION) {
      log += write_yaml_form(log, 2, "ambisonics_projection_config:");
      log += write_yaml_form(log, 3, "output_channel_count: %u",
                             ambisonics_conf->output_channel_count);
      log += write_yaml_form(log, 3, "substream_count: %u",
                             ambisonics_conf->substream_count);
      log += write_yaml_form(log, 3, "coupled_substream_count: %u",
                             ambisonics_conf->coupled_substream_count);
      log += write_yaml_form(log, 3, "demixing_matrix:");
      for (uint64_t i = 0; i < ambisonics_conf->mapping_size; i += 2) {
        int16_t value = (ambisonics_conf->mapping[i] << 8) |
                        ambisonics_conf->mapping[i + 1];
        log += write_yaml_form(log, 3, "- %d", value);
      }
    }
  }

  write_postfix(LOG_OBU, log);
}

static void write_mix_presentation_log(uint64_t idx, void* obu, char* log) {
  IAMF_MixPresentation* mp_obu = (IAMF_MixPresentation*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "MixPresentationOBU_%llu:", idx);
  log += write_yaml_form(log, 0, "- mix_presentation_id: %llu",
                         mp_obu->mix_presentation_id);
  log += write_yaml_form(log, 1, "mix_presentation_annotations:");
  log += write_yaml_form(log, 2, "mix_presentation_friendly_label: \"%s\"",
                         mp_obu->mix_presentation_friendly_label);
  log += write_yaml_form(log, 1, "num_sub_mixes: %llu", mp_obu->num_sub_mixes);
  log += write_yaml_form(log, 1, "sub_mixes:");
  for (uint64_t i = 0; i < mp_obu->num_sub_mixes; ++i) {
    SubMixPresentation* submix = &mp_obu->sub_mixes[i];
    log += write_yaml_form(log, 1, "- num_audio_elements: %llu",
                           submix->nb_elements);
    log += write_yaml_form(log, 2, "audio_elements:");
    for (uint64_t j = 0; j < submix->nb_elements; ++j) {
      ElementMixRenderConf* conf_s = &submix->conf_s[j];
      log += write_yaml_form(log, 2, "- audio_element_id: %llu",
                             conf_s->element_id);
      log += write_yaml_form(log, 3, "mix_presentation_element_annotations:");
      log += write_yaml_form(log, 4, "audio_element_friendly_label: \"%s\"",
                             conf_s->audio_element_friendly_label);

      // log += write_yaml_form(log, 2, "- rendering_config:");

      log += write_yaml_form(log, 3, "element_mix_config:");
      log += write_yaml_form(log, 4, "mix_gain:");
      log += write_yaml_form(log, 5, "param_definition:");
      log += write_yaml_form(log, 6, "parameter_id: %llu",
                             conf_s->conf_m.gain.base.id);
      log += write_yaml_form(log, 6, "parameter_rate: %llu",
                             conf_s->conf_m.gain.base.rate);
      log += write_yaml_form(log, 6, "param_definition_mode: %u",
                             conf_s->conf_m.gain.base.mode);
      if (conf_s->conf_m.gain.base.mode == 0) {
        log += write_yaml_form(log, 6, "duration: %llu",
                               conf_s->conf_m.gain.base.duration);
        log += write_yaml_form(log, 6, "num_subblocks: %llu",
                               conf_s->conf_m.gain.base.nb_segments);
        log +=
            write_yaml_form(log, 6, "constant_subblock_duration: %llu",
                            conf_s->conf_m.gain.base.constant_segment_interval);
        if (conf_s->conf_m.gain.base.constant_segment_interval == 0) {
          log += write_yaml_form(log, 6, "subblock_durations:");
          for (uint64_t k = 0; k < conf_s->conf_m.gain.base.nb_segments; ++k) {
            log += write_yaml_form(
                log, 6, "- %llu",
                conf_s->conf_m.gain.base.segments->segment_interval);
          }
        }
      }
      log += write_yaml_form(log, 5, "default_mix_gain: %d",
                             conf_s->conf_m.gain.mix_gain);
    }

    OutputMixConf* output_mix_config = &submix->output_mix_config;
    log += write_yaml_form(log, 2, "output_mix_config:");
    log += write_yaml_form(log, 3, "output_mix_gain:");
    log += write_yaml_form(log, 4, "param_definition:");
    log += write_yaml_form(log, 5, "parameter_id: %llu",
                           output_mix_config->gain.base.id);
    log += write_yaml_form(log, 5, "parameter_rate: %llu",
                           output_mix_config->gain.base.rate);
    log += write_yaml_form(log, 5, "param_definition_mode: %u",
                           output_mix_config->gain.base.mode);
    if (output_mix_config->gain.base.mode == 0) {
      log += write_yaml_form(log, 6, "duration: %llu",
                             output_mix_config->gain.base.duration);
      log += write_yaml_form(log, 6, "num_subblocks: %llu",
                             output_mix_config->gain.base.nb_segments);
      log += write_yaml_form(
          log, 6, "constant_subblock_duration: %llu",
          output_mix_config->gain.base.constant_segment_interval);
      if (output_mix_config->gain.base.constant_segment_interval == 0) {
        log += write_yaml_form(log, 6, "subblock_durations:");
        for (uint64_t k = 0; k < output_mix_config->gain.base.nb_segments;
             ++k) {
          log += write_yaml_form(
              log, 6, "- %llu",
              output_mix_config->gain.base.segments->segment_interval);
        }
      }
    }
    log += write_yaml_form(log, 4, "default_mix_gain: %d",
                           output_mix_config->gain.mix_gain);

    log += write_yaml_form(log, 2, "num_layouts: %llu", submix->num_layouts);

    log += write_yaml_form(log, 2, "layouts:");
    for (uint64_t j = 0; j < submix->num_layouts; ++j) {
      // layout
      log += write_yaml_form(log, 2, "- loudness_layout:");

      uint32_t layout_type = submix->layouts[j]->type;
      log += write_yaml_form(log, 4, "layout_type: %lu", layout_type);

      if (layout_type == IAMF_LAYOUT_TYPE_LOUDSPEAKERS_SP_LABEL) {
        log += write_yaml_form(log, 4, "sp_layout:");

        SP_Label_Layout* sp = SP_LABEL_LAYOUT(submix->layouts[j]);
        log += write_yaml_form(log, 5, "num_loudspeakers: %lu",
                               sp->nb_loudspeakers);
        log += write_yaml_form(log, 5, "sp_labels:");
        for (uint8_t k = 0; k < sp->nb_loudspeakers; ++k) {
          log += write_yaml_form(log, 5, "- %u", sp->sp_labels[k]);
        }
        // __KWON_TODO
      } else if (layout_type == IAMF_LAYOUT_TYPE_LOUDSPEAKERS_SS_CONVENTION) {
        log += write_yaml_form(log, 4, "ss_layout:");

        SoundSystemLayout* ss = SOUND_SYSTEM_LAYOUT(submix->layouts[j]);
        log += write_yaml_form(log, 5, "sound_system: %u", ss->sound_system);
      }

      // loudness
      log += write_yaml_form(log, 3, "loudness:");
      log += write_yaml_form(log, 4, "info_type: %u",
                             submix->loudness[j].info_type);
      log += write_yaml_form(log, 4, "integrated_loudness: %d",
                             submix->loudness[j].integrated_loudness);
      log += write_yaml_form(log, 4, "digital_peak: %d",
                             submix->loudness[j].digital_peak);
    }
  }
  write_postfix(LOG_OBU, log);
}

static void write_parameter_block_log(uint64_t idx, void* obu, char* log) {
  IAMF_Parameter* para = (IAMF_Parameter*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "ParameterBlockOBU_%llu:", idx);
  log += write_yaml_form(log, 0, "- parameter_id: %llu", para->id);
  log += write_yaml_form(log, 1, "duration: %llu", para->duration);
  log += write_yaml_form(log, 1, "num_subblocks: %llu", para->nb_segments);
  log += write_yaml_form(log, 1, "constant_subblock_duration: %llu",
                         para->constant_segment_interval);
  log += write_yaml_form(log, 1, "subblocks:");
  for (uint64_t i = 0; i < para->nb_segments; ++i) {
    if (para->type == IAMF_PARAMETER_TYPE_MIX_GAIN) {
      MixGainSegment* mg = (MixGainSegment*)para->segments[i];
      log += write_yaml_form(log, 1, "- mix_gain_parameter_data:");
      log += write_yaml_form(log, 3, "subblock_duration: %llu",
                             mg->seg.segment_interval);
      log += write_yaml_form(log, 3, "animation_type: %llu",
                             mg->mix_gain.animated_type);
      log += write_yaml_form(log, 3, "param_data:");
      if (mg->mix_gain.animated_type == PARAMETER_ANIMATED_TYPE_STEP) {
        log += write_yaml_form(log, 4, "step:");
        log += write_yaml_form(log, 5, "start_point_value: %d",
                               mg->mix_gain.start);
      } else if (mg->mix_gain.animated_type == PARAMETER_ANIMATED_TYPE_LINEAR) {
        log += write_yaml_form(log, 4, "linear:");
        log += write_yaml_form(log, 5, "start_point_value: %d",
                               mg->mix_gain.start);
        log += write_yaml_form(log, 5, "end_point_value: %d", mg->mix_gain.end);
      } else if (mg->mix_gain.animated_type == PARAMETER_ANIMATED_TYPE_BEZIER) {
        log += write_yaml_form(log, 4, "bezier:");
        log += write_yaml_form(log, 5, "start_point_value: %d",
                               mg->mix_gain.start);
        log += write_yaml_form(log, 5, "end_point_value: %d", mg->mix_gain.end);
        log += write_yaml_form(log, 5, "control_point_value: %d",
                               mg->mix_gain.control);
        log += write_yaml_form(log, 5, "control_point_relative_time: %u",
                               mg->mix_gain.control_relative_time & 0xFF);
      }
    } else if (para->type == IAMF_PARAMETER_TYPE_DEMIXING) {
      DemixingSegment* mode = (DemixingSegment*)para->segments[i];
      log += write_yaml_form(log, 1, "- demixing_info_parameter_data:");
      log += write_yaml_form(log, 3, "subblock_duration: %llu",
                             mode->seg.segment_interval);
      log += write_yaml_form(log, 3, "dmixp_mode: %lu", mode->demixing_mode);
    } else if (para->type == IAMF_PARAMETER_TYPE_RECON_GAIN) {
      log += write_yaml_form(log, 1, "- recon_gain_parameter_data:");
      // log += write_yaml_form(log, 3, "subblock_duration: %llu",
      // ->seg->segment_interval); KWON_TODO
    }
  }

  write_postfix(LOG_OBU, log);
}

static void write_audio_frame_log(uint64_t idx, void* obu, char* log,
                                  uint64_t num_samples_to_trim_at_start,
                                  uint64_t num_samples_to_trim_at_end) {
  IAMF_Frame* af_obu = (IAMF_Frame*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "AudioFrameOBU_%llu:", idx);
  log += write_yaml_form(log, 0, "- audio_substream_id: %llu", af_obu->id);
  log += write_yaml_form(log, 1, "num_samples_to_trim_at_start: %llu",
                         num_samples_to_trim_at_start);
  log += write_yaml_form(log, 1, "num_samples_to_trim_at_end: %llu",
                         num_samples_to_trim_at_end);
  log += write_yaml_form(log, 1, "size_of(audio_frame): %u", af_obu->size);
  write_postfix(LOG_OBU, log);
}

static void write_temporal_delimiter_block_log(uint64_t idx, void* obu,
                                               char* log) {
  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "TemporalDelimiterOBU_%llu:", idx);
  write_postfix(LOG_OBU, log);
}

static void write_sync_log(uint64_t idx, void* obu, char* log) {
  IAMF_Sync* sc_obu = (IAMF_Sync*)obu;

  log += write_prefix(LOG_OBU, log);
  log += write_yaml_form(log, 0, "SyncOBU_%llu:", idx);
  log +=
      write_yaml_form(log, 0, "- global_offset: %llu", sc_obu->global_offset);
  log += write_yaml_form(log, 1, "num_obu_ids: %llu", sc_obu->nb_obu_ids);

  log += write_yaml_form(log, 1, "sync_array:");
  for (uint64_t i = 0; i < sc_obu->nb_obu_ids; ++i) {
    log += write_yaml_form(log, 1, "- obu_id: %llu", sc_obu->objs[i].obu_id);
    log += write_yaml_form(log, 2, "obu_data_type: %u",
                           sc_obu->objs[i].obu_data_type);
    log += write_yaml_form(log, 2, "reinitialize_decoder: %u",
                           sc_obu->objs[i].reinitialize_decoder);
    log += write_yaml_form(log, 2, "relative_offset: %d",
                           sc_obu->objs[i].relative_offset);
  }
  write_postfix(LOG_OBU, log);
}

int vlog_obu(uint32_t obu_type, void* obu,
             uint64_t num_samples_to_trim_at_start,
             uint64_t num_samples_to_trim_at_end) {
  if (!is_vlog_file_open()) return -1;

  static uint64_t obu_count = 0;
  static char log[LOG_BUFFER_SIZE];
  uint64_t key;

  log[0] = 0;
  key = obu_count;

  switch (obu_type) {
    case IAMF_OBU_CODEC_CONFIG:
      write_codec_config_log(obu_count++, obu, log);
      break;
    case IAMF_OBU_AUDIO_ELEMENT:
      write_audio_element_log(obu_count++, obu, log);
      break;
    case IAMF_OBU_MIX_PRESENTATION:
      write_mix_presentation_log(obu_count++, obu, log);
      break;
    case IAMF_OBU_PARAMETER_BLOCK:
      write_parameter_block_log(obu_count++, obu, log);
      break;
    case IAMF_OBU_TEMPORAL_DELIMITER:
      write_temporal_delimiter_block_log(obu_count++, obu, log);
      break;
    case IAMF_OBU_SYNC:
      write_sync_log(obu_count++, obu, log);
      break;
    case IAMF_OBU_MAGIC_CODE:
      write_magic_code_log(obu_count++, obu, log);
      break;
    default:
      if (obu_type >= IAMF_OBU_AUDIO_FRAME &&
          obu_type <= IAMF_OBU_AUDIO_FRAME_ID21) {
        write_audio_frame_log(obu_count++, obu, log,
                              num_samples_to_trim_at_start,
                              num_samples_to_trim_at_end);
      }
      break;
  }

  return vlog_print(LOG_OBU, key, log);
}
