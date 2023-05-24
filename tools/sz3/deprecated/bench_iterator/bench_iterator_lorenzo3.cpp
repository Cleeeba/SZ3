//
// Created by Kai Zhao on 1/25/23.
//

#include "SZ3/api/sz.hpp"

/*
 * no padding
 * slower than global or local padding
 */

using namespace SZ;

template<class T>
inline __attribute__((always_inline)) T d(T *data, size_t ds0, size_t ds1, int i, int j, int k) {
    if (i < 0 || j < 0 || k < 0) {
        return 0;
    } else {
        return data[i * ds0 + j * ds1 + k];
    }
}

template<class T, uint N>
uchar *compress(Config &conf, T *data_, size_t &compressed_size) {

    std::vector<T> unpred;
    unpred.reserve(conf.num);

    Timer timer(true);
    std::vector<int> quant_inds;
    quant_inds.reserve(conf.num);
    LinearQuantizer<T> quantizer(conf.absErrorBound);

    size_t ds0 = conf.dims[2] * conf.dims[1];
    size_t ds1 = conf.dims[2];

    size_t bsize = 6;
    std::vector<T> data1(data_, data_ + conf.num);
    T *data = data1.data();

    auto blocks = std::make_shared<SZ::multi_dimensional_range<T, N>>(data, std::begin(conf.dims), std::end(conf.dims), bsize, 0);
    for (auto block = blocks->begin(); block != blocks->end(); ++block) {
        {
            auto idx = block.get_global_index();
            for (int i = idx[0]; i < std::min(conf.dims[0], idx[0] + bsize); i++) {
                for (int j = idx[1]; j < std::min(conf.dims[1], idx[1] + bsize); j++) {
                    for (int k = idx[2]; k < std::min(conf.dims[2], idx[2] + bsize); k++) {
                        size_t offset = i * ds0 + j * ds1 + k;
                        T *data_pos = &data[offset];
                        bool ii = (i != 0);
                        bool jj = (j != 0);
                        bool kk = (k != 0);
                        T pred = 0;
                        if (ii) {
                            pred += data_pos[-ds0];
                        }
                        if (jj) {
                            pred += data_pos[-ds1];
                        }
                        if (kk) {
                            pred += data_pos[-1];
                        }
                        if (ii && jj) {
                            pred -= data_pos[-ds0 - ds1];
                        }
                        if (jj && kk) {
                            pred -= data_pos[-ds1 - 1];
                        }
                        if (ii && kk) {
                            pred -= data_pos[-ds0 - 1];
                        }
                        if (ii && jj && kk) {
                            pred += data_pos[-ds0 - ds1 - 1];
                        }
                        quant_inds.push_back(quantizer.quantize_and_overwrite(data[offset], pred));
//                        quant_inds.push_back(quantizer.quantize_and_overwrite(datap[offset_], pred));
                        //                        quant_inds_3[offset] = quantize_and_overwrite<T>(data_[offset], 0, unpred, error_bound, error_bound_reciprocal, radius);
                    }
                }
            }
        }
    }
    timer.stop("frondend compress");

    HuffmanEncoder<int> encoder;
    encoder.preprocess_encode(quant_inds, 0);
//    size_t bufferSize = 1.2 * (encoder.size_est() + sizeof(T) * quant_inds.size() + unpred.size() * sizeof(T));
    size_t bufferSize = 1.2 * (encoder.size_est() + sizeof(T) * quant_inds.size() + quantizer.size_est());

    uchar *buffer = new uchar[bufferSize];
    uchar *buffer_pos = buffer;

//    quantizer.save(buffer_pos);
    *reinterpret_cast<size_t *>(buffer_pos) = unpred.size();
    buffer_pos += sizeof(size_t);
    memcpy(buffer_pos, unpred.data(), unpred.size() * sizeof(T));
    buffer_pos += unpred.size() * sizeof(T);

    encoder.save(buffer_pos);
    encoder.encode(quant_inds, buffer_pos);
    encoder.postprocess_encode();
    assert(buffer_pos - buffer < bufferSize);
//            timer.stop("huffman");

//            timer.start();
    Lossless_zstd lossless;
    uchar *lossless_data = lossless.compress(buffer, buffer_pos - buffer, compressed_size);
    lossless.postcompress_data(buffer);
//            timer.stop("lossless");


    return lossless_data;
}

template<class T, uint N>
void decompress(Config &conf, uchar const *cmpData, const size_t &cmpSize, T *data) {
    size_t remaining_length = cmpSize;
    LinearQuantizer<T> quantizer(conf.absErrorBound);

    Lossless_zstd lossless;
    auto compressed_data = lossless.decompress(cmpData, remaining_length);
    uchar const *compressed_data_pos = compressed_data;
//            timer.stop("Lossless");

//    quantizer.load(compressed_data_pos, remaining_length);
    size_t unpred_size = *reinterpret_cast<const size_t *>(compressed_data_pos);
    compressed_data_pos += sizeof(size_t);
    auto unpred = std::vector<T>(reinterpret_cast<const T *>(compressed_data_pos), reinterpret_cast<const T *>(compressed_data_pos) + unpred_size);
    compressed_data_pos += unpred_size * sizeof(T);
    size_t unpred_index = 0;


    HuffmanEncoder<int> encoder;
    encoder.load(compressed_data_pos, remaining_length);

    auto quant_inds = encoder.decode(compressed_data_pos, conf.num);
    encoder.postprocess_decode();
//            timer.stop("Decoder");

    lossless.postdecompress_data(compressed_data);

    size_t ds0 = conf.dims[2] * conf.dims[1];
    size_t ds1 = conf.dims[2];

    int *quant_inds_pos = &quant_inds[0];

    Timer timer(true);
    size_t bsize = 6;
    auto blocks = std::make_shared<SZ::multi_dimensional_range<T, N>>(data, std::begin(conf.dims), std::end(conf.dims), bsize, 0);
    for (auto block = blocks->begin(); block != blocks->end(); ++block) {
        {
            auto idx = block.get_global_index();
            for (size_t i = idx[0]; i < std::min(conf.dims[0], idx[0] + bsize); i++) {
                for (size_t j = idx[1]; j < std::min(conf.dims[1], idx[1] + bsize); j++) {
                    for (size_t k = idx[2]; k < std::min(conf.dims[2], idx[2] + bsize); k++) {
                        size_t offset = i * ds0 + j * ds1 + k;
                        T *data_pos = &data[offset];
                        bool ii = (i != 0);
                        bool jj = (j != 0);
                        bool kk = (k != 0);
                        T pred = 0;
                        if (ii) {
                            pred += data_pos[-ds0];
                        }
                        if (jj) {
                            pred += data_pos[-ds1];
                        }
                        if (kk) {
                            pred += data_pos[-1];
                        }
                        if (ii && jj) {
                            pred -= data_pos[-ds0 - ds1];
                        }
                        if (jj && kk) {
                            pred -= data_pos[-ds1 - 1];
                        }
                        if (ii && kk) {
                            pred -= data_pos[-ds0 - 1];
                        }
                        if (ii && jj && kk) {
                            pred += data_pos[-ds0 - ds1 - 1];
                        }
                        if (*quant_inds_pos) {
                            data[offset] = quantizer.recover_pred(pred, *quant_inds_pos);
                        } else {
                            data[offset] = unpred[unpred_index++];
                        }
                        quant_inds_pos++;
                    }
                }
            }
        }
    }
    timer.stop("frondend decompress");
}

template<class T, uint N>
void estimate_compress(Config &conf, T *data) {
    conf.absErrorBound = 1e-2;

    Timer timer(true);
    size_t cmpr_size;
    auto cmpr_data = compress<T, N>(conf, data, cmpr_size);
    timer.stop("compress");

    T *decData = new T[conf.num];
    timer.start();
    decompress<T, N>(conf, cmpr_data, cmpr_size, decData);
    timer.stop("decompress");

    printf("CR= %.3f\n", conf.num * sizeof(T) * 1.0 / cmpr_size);
    SZ::verify(data, decData, conf.num);
};

int main(int argc, char *argv[]) {

    if (argc < 4) {
        std::cout << "usage: " << argv[0] << " data_file dim dim0 .. dimn" << std::endl;
        std::cout << "example: " << argv[0] << " qmcpack.dat 3 33120 69 69" << std::endl;
        return 0;
    }

    size_t num = 0;
    auto data = SZ::readfile<float>(argv[1], num);

    int dim = atoi(argv[2]);
    if (dim == 1) {
        Config config(atoi(argv[3]));
        estimate_compress<float, 1>(config, data.get());
    } else if (dim == 2) {
        Config config(atoi(argv[3]), atoi(argv[4]));
        estimate_compress<float, 2>(config, data.get());
    } else if (dim == 3) {
        Config config(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
        estimate_compress<float, 3>(config, data.get());
    } else if (dim == 4) {
        Config config(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
        estimate_compress<float, 4>(config, data.get());
    }

}