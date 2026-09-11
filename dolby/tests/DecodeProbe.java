import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import java.nio.ByteBuffer;

public class DecodeProbe {
    public static void main(String[] args) throws Exception {
        for (int run = 0; run < 3; run++) {
            MediaExtractor extractor = new MediaExtractor();
            MediaCodec codec = null;
            try {
                extractor.setDataSource(args[0]);
                extractor.selectTrack(0);
                MediaFormat format = extractor.getTrackFormat(0);
                codec = MediaCodec.createByCodecName("c2.dolby.eac3.decoder");
                codec.configure(format, null, null, 0);
                codec.start();
                MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
                boolean inputDone = false, outputDone = false;
                long bytes = 0, nonzero = 0;
                long deadline = System.nanoTime() + 15_000_000_000L;
                while (!outputDone && System.nanoTime() < deadline) {
                    if (!inputDone) {
                        int index = codec.dequeueInputBuffer(10000);
                        if (index >= 0) {
                            ByteBuffer input = codec.getInputBuffer(index);
                            input.clear();
                            int size = extractor.readSampleData(input, 0);
                            if (size < 0) {
                                codec.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                                inputDone = true;
                            } else {
                                codec.queueInputBuffer(index, 0, size, extractor.getSampleTime(), 0);
                                extractor.advance();
                            }
                        }
                    }
                    int index = codec.dequeueOutputBuffer(info, 10000);
                    if (index >= 0) {
                        ByteBuffer output = codec.getOutputBuffer(index);
                        for (int i = info.offset; i < info.offset + info.size; i++) {
                            if (output.get(i) != 0) nonzero++;
                        }
                        bytes += info.size;
                        outputDone = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                        codec.releaseOutputBuffer(index, false);
                    }
                }
                if (!outputDone || bytes < 300000 || nonzero == 0) {
                    throw new IllegalStateException("EOS=" + outputDone + " bytes=" + bytes + " nonzero=" + nonzero);
                }
                System.out.println("PASS run=" + run + " bytes=" + bytes + " nonzero=" + nonzero
                        + " format=" + codec.getOutputFormat());
                codec.stop();
            } finally {
                if (codec != null) codec.release();
                extractor.release();
            }
        }
    }
}
