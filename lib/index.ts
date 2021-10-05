interface Decoder {
  decode(buffer: Buffer): Buffer;
  setup(header: Buffer): void;
}

const VorbisDecoder: Decoder = require('../build/Release/vorbis-dec');

export { VorbisDecoder }