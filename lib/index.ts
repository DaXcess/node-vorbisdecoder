interface Decoder {
  decode(buffer: Buffer): Buffer;
  setup(header: Buffer): void;
}

const VorbisDecoder: Decoder = require(`${__dirname}/Release/vorbis-dec`);

export { VorbisDecoder }