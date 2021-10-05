#include <nan.h>
#include <math.h>
#include <vorbis/codec.h>

using namespace v8;

typedef struct _DATA_BLOB
{
  size_t cbData;
  unsigned char *pbData;
} DATA_BLOB, *PDATA_BLOB;

namespace vorbisdecoder
{
  ogg_sync_state oy;
  ogg_stream_state os;

  ogg_page og;
  ogg_packet op;

  vorbis_info vi;
  vorbis_comment vc;
  vorbis_dsp_state vd;
  vorbis_block vb;

  ogg_int16_t convbuffer[4096];
  int convsize = 4096;

  bool headers_ready = false;

  NAN_METHOD(SetupHeader)
  {
    if (headers_ready) {
      vorbis_block_clear(&vb);
      vorbis_dsp_clear(&vd);

      ogg_stream_clear(&os);
      vorbis_comment_clear(&vc);
      vorbis_info_clear(&vi);

      ogg_sync_clear(&oy);

      headers_ready = false;
    }

    auto context = info.GetIsolate()->GetCurrentContext();
    auto node_buffer = info[0]->ToObject(context).ToLocalChecked();

    char *cbuffer = (char *)node::Buffer::Data(node_buffer);
    unsigned long size = node::Buffer::Length(node_buffer);
    unsigned long offset = 0;

    char *buffer;
    int bytes;

    ogg_sync_init(&oy);

    int eos = 0;
    int i;

    bytes = MIN(4096, size - offset);

    buffer = ogg_sync_buffer(&oy, bytes);
    memcpy(buffer, cbuffer + offset, bytes);
    ogg_sync_wrote(&oy, bytes);

    offset += bytes;

    if (ogg_sync_pageout(&oy, &og) != 1)
    {
      if (bytes < 4096)
      {
        ogg_sync_clear(&oy);

        Nan::ThrowError("Not enough data for ogg header");
      }

      Nan::ThrowError("Input does not appear to be an Ogg bitstream");
      return;
    }

    ogg_stream_init(&os, ogg_page_serialno(&og));

    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    if (ogg_stream_pagein(&os, &og) < 0)
    {
      Nan::ThrowError("Error reading first page of Ogg bitstream data.");
      return;
    }

    if (ogg_stream_packetout(&os, &op) != 1)
    {
      Nan::ThrowError("Error reading initial header packet.");
      return;
    }

    if (vorbis_synthesis_headerin(&vi, &vc, &op) < 0)
    {
      Nan::ThrowError("This Ogg bitstream does not contain Vorbis audio data.");
      return;
    }

    i = 0;
    while (i < 2)
    {
      while (i < 2)
      {
        int result = ogg_sync_pageout(&oy, &og);
        if (result == 0)
          break;

        if (result == 1)
        {
          ogg_stream_pagein(&os, &og);

          while (i < 2)
          {
            result = ogg_stream_packetout(&os, &op);
            if (result == 0)
              break;
            if (result < 0)
            {
              Nan::ThrowError("Corrupt secondary header.");
              return;
            }
            result = vorbis_synthesis_headerin(&vi, &vc, &op);
            if (result < 0)
            {
              Nan::ThrowError("Corrupt secondary header.");
              return;
            }
            i++;
          }
        }
      }

      bytes = MIN(4096, size - offset);

      buffer = ogg_sync_buffer(&oy, bytes);
      memcpy(buffer, cbuffer + offset, bytes);
      offset += bytes;

      if (bytes == 0 && i < 2)
      {
        Nan::ThrowError("End of file before finding all Vorbis headers!");
        return;
      }

      ogg_sync_wrote(&oy, bytes);
    }

    convsize = 4096 / vi.channels;

    if (vorbis_synthesis_init(&vd, &vi) == 0)
    {
      vorbis_block_init(&vd, &vb);

      headers_ready = true;
    }
  }

  NAN_METHOD(DecodeBuffer)
  {
    auto context = info.GetIsolate()->GetCurrentContext();
    auto node_buffer = info[0]->ToObject(context).ToLocalChecked();

    if (!headers_ready) {
      Nan::ThrowError("Cannot decode before headers are setup");
      return;
    }

    char *cbuffer = (char *)node::Buffer::Data(node_buffer);
    unsigned long size = node::Buffer::Length(node_buffer);
    unsigned long offset = 0;

    char *buffer;
    int bytes;

    std::vector<DATA_BLOB> returnBuffers;

    int eos = 0;
    int i;

    while (!eos)
    {
      while (!eos)
      {
        int result = ogg_sync_pageout(&oy, &og);
        if (result == 0)
          break;
        if (result < 0)
        // {
        //   fprintf(stderr, "Corrupt or missing data in bitstream; %d -> %d\n", offset, size);
          break;
        else
        {
          ogg_stream_pagein(&os, &og);

          while (1)
          {
            result = ogg_stream_packetout(&os, &op);

            if (result == 0)
              break;
            if (result < 0)
            {
            }
            else
            {
              float **pcm;
              int samples;

              if (vorbis_synthesis(&vb, &op) == 0)
                vorbis_synthesis_blockin(&vd, &vb);

              while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0)
              {
                int j;
                int clipflag = 0;
                int bout = (samples < convsize ? samples : convsize);

                for (i = 0; i < vi.channels; i++)
                {
                  ogg_int16_t *ptr = convbuffer + i;
                  float *mono = pcm[i];
                  for (j = 0; j < bout; j++)
                  {
                    int val = floor(mono[j] * 32767.f + 0.5f);
                    //int val = mono[j] * 32767.f + drand48() - 0.5f;

                    if (val > 32767)
                    {
                      val = 32767;
                      clipflag = 1;
                    }
                    if (val < -32768)
                    {
                      val = -32768;
                      clipflag = 1;
                    }
                    *ptr = val;
                    ptr += vi.channels;
                  }
                }

                // if (clipflag)
                //   fprintf(stderr, "Clipping in frame %ld\n", (long)(vd.sequence));

                DATA_BLOB blob = {0};
                blob.cbData = bout * 2 * vi.channels;
                blob.pbData = (unsigned char *)malloc(bout * 2 * vi.channels);
                memcpy(blob.pbData, convbuffer, bout * 2 * vi.channels);

                returnBuffers.push_back(blob);

                vorbis_synthesis_read(&vd, bout);
              }
            }
          }
          if (ogg_page_eos(&og))
            eos = 1;
        }
      }

      if (!eos)
      {
        bytes = MIN(4096, size - offset);
        buffer = ogg_sync_buffer(&oy, 4096);
        memcpy(buffer, cbuffer + offset, bytes);
        offset += bytes;

        ogg_sync_wrote(&oy, bytes);
        if (bytes == 0)
          break;
      }
    }

    if (eos)
    {
      vorbis_block_clear(&vb);
      vorbis_dsp_clear(&vd);

      ogg_stream_clear(&os);
      vorbis_comment_clear(&vc);
      vorbis_info_clear(&vi);

      ogg_sync_clear(&oy);

      headers_ready = false;
    }

    size_t totalSize = 0;
    for (auto i = 0; i < returnBuffers.size(); i++)
    {
      totalSize += returnBuffers.at(i).cbData;
    }

    auto buf = Nan::NewBuffer(totalSize).ToLocalChecked();
    auto bufData = node::Buffer::Data(buf);

    offset = 0;

    for (auto i = 0; i < returnBuffers.size(); i++)
    {
      memcpy(bufData + offset, returnBuffers.at(i).pbData, returnBuffers.at(i).cbData);
      offset += returnBuffers.at(i).cbData;
    }

    info.GetReturnValue().Set(buf);
  }

  NAN_MODULE_INIT(InitModule)
  {
    Nan::SetMethod(target, "setup", SetupHeader);
    Nan::SetMethod(target, "decode", DecodeBuffer);
  }
}

NODE_MODULE(VorbisDecoder, vorbisdecoder::InitModule);