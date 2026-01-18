#include "SdfWorker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include <QImage>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <QtGui/qrgb.h>

namespace sdf_internal {
constexpr double kInfinity = 1e20;

int effectiveThreadCount(int totalBlocks)
{
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        hw = 4;
    }
    return std::max(1, std::min(static_cast<int>(hw), totalBlocks));
}

int chooseBlockSize(int totalItems)
{
    if (totalItems <= 0) {
        return 1;
    }
    const int threads = effectiveThreadCount(totalItems);
    int size = totalItems / (threads * 4);
    if (size < 1) {
        size = 1;
    }
    return size;
}

template <typename BlockFn, typename ProgressFn>
bool runParallelBlocks(
    int totalItems,
    int blockSize,
    std::atomic_bool *cancel,
    const BlockFn &blockFn,
    const ProgressFn &progressFn)
{
    if (totalItems <= 0) {
        return true;
    }
    if (blockSize <= 0) {
        blockSize = chooseBlockSize(totalItems);
    }

    const int totalBlocks = (totalItems + blockSize - 1) / blockSize;
    const int threadCount = effectiveThreadCount(totalBlocks);
    std::atomic<int> nextBlock(0);
    std::atomic<int> blocksDone(0);
    std::mutex mutex;
    std::condition_variable cv;

    auto workerFn = [&]() {
        while (true) {
            if (cancel && cancel->load()) {
                break;
            }
            const int block = nextBlock.fetch_add(1);
            if (block >= totalBlocks) {
                break;
            }
            const int start = block * blockSize;
            const int end = std::min(start + blockSize, totalItems);
            blockFn(start, end);
            blocksDone.fetch_add(1);
            cv.notify_all();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(threadCount));
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(workerFn);
    }

    int reportedItems = 0;
    while (true) {
        if (cancel && cancel->load()) {
            break;
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, std::chrono::milliseconds(30));
        }
        const int doneBlocks = blocksDone.load();
        const int completedItems = std::min(doneBlocks * blockSize, totalItems);
        if (completedItems > reportedItems) {
            progressFn(completedItems - reportedItems);
            reportedItems = completedItems;
        }
        if (doneBlocks >= totalBlocks) {
            break;
        }
    }

    for (auto &thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return !(cancel && cancel->load());
}

void distanceTransform1D(const QVector<double> &f, QVector<double> &d, int n)
{
    if (n <= 0) {
        return;
    }

    QVector<int> v(n);
    QVector<double> z(n + 1);
    int k = 0;
    v[0] = 0;
    z[0] = -kInfinity;
    z[1] = kInfinity;

    for (int q = 1; q < n; ++q) {
        double s = 0.0;
        while (k >= 0) {
            const int vk = v[k];
            s = ((f[q] + q * q) - (f[vk] + vk * vk)) / (2.0 * (q - vk));
            if (s > z[k]) {
                break;
            }
            --k;
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = kInfinity;
    }

    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) {
            ++k;
        }
        const int vk = v[k];
        const double dx = q - vk;
        d[q] = dx * dx + f[vk];
    }
}

QVector<double> distanceTransform2D(
    const QVector<double> &f,
    int width,
    int height,
    std::atomic_bool *cancel,
    const std::function<void(int)> &progressFn)
{
    if (width <= 0 || height <= 0 || f.size() != width * height) {
        return {};
    }

    QVector<double> tmp(width * height);
    QVector<double> out(width * height);

    const auto rowBlock = [&](int start, int end) {
        QVector<double> rowIn(width);
        QVector<double> rowOut(width);
        for (int y = start; y < end; ++y) {
            if (cancel && cancel->load()) {
                return;
            }
            const int rowOffset = y * width;
            for (int x = 0; x < width; ++x) {
                rowIn[x] = f[rowOffset + x];
            }
            distanceTransform1D(rowIn, rowOut, width);
            for (int x = 0; x < width; ++x) {
                tmp[rowOffset + x] = rowOut[x];
            }
        }
    };

    const int rowBlockSize = chooseBlockSize(height);
    if (!runParallelBlocks(height, rowBlockSize, cancel, rowBlock, progressFn)) {
        return {};
    }

    const auto colBlock = [&](int start, int end) {
        QVector<double> colIn(height);
        QVector<double> colOut(height);
        for (int x = start; x < end; ++x) {
            if (cancel && cancel->load()) {
                return;
            }
            for (int y = 0; y < height; ++y) {
                colIn[y] = tmp[y * width + x];
            }
            distanceTransform1D(colIn, colOut, height);
            for (int y = 0; y < height; ++y) {
                out[y * width + x] = colOut[y];
            }
        }
    };

    const int colBlockSize = chooseBlockSize(width);
    if (!runParallelBlocks(width, colBlockSize, cancel, colBlock, progressFn)) {
        return {};
    }

    return out;
}
} // namespace sdf_internal

SdfWorker::SdfWorker(const QImage &input, int outW, int outH, int threshold, int maxDist)
    : inputImage(input)
    , outWidth(outW)
    , outHeight(outH)
    , thresholdValue(threshold)
    , maxDistance(maxDist)
    , cancelRequested(false)
{
}

void SdfWorker::requestCancel()
{
    cancelRequested.store(true);
}

void SdfWorker::process()
{
    if (inputImage.isNull()) {
        emit failed(tr("Input image is empty."));
        return;
    }
    if (outWidth <= 0 || outHeight <= 0) {
        emit failed(tr("Invalid output size."));
        return;
    }

    QImage source = inputImage.convertToFormat(QImage::Format_Grayscale8);
    if (source.isNull()) {
        emit failed(tr("Failed to convert image format."));
        return;
    }

    const int inW = source.width();
    const int inH = source.height();
    if (inW <= 0 || inH <= 0) {
        emit failed(tr("Input image has invalid size."));
        return;
    }

    const int totalUnits = (5 * outHeight) + (2 * outWidth);
    int completedUnits = 0;
    auto reportUnits = [&](int delta) {
        if (totalUnits <= 0) {
            emit progress(100);
            return;
        }
        completedUnits += delta;
        if (completedUnits > totalUnits) {
            completedUnits = totalUnits;
        }
        const int pct = (completedUnits * 100) / totalUnits;
        emit progress(pct);
    };

    QVector<uchar> insideMask(outWidth * outHeight);
    for (int oy = 0; oy < outHeight; ++oy) {
        if (cancelRequested.load()) {
            emit canceled();
            return;
        }
        const int cy = (oy * inH) / outHeight;
        const uchar *row = source.constScanLine(cy);
        for (int ox = 0; ox < outWidth; ++ox) {
            const int cx = (ox * inW) / outWidth;
            const uchar gray = row[cx];
            insideMask[ox + oy * outWidth] = (gray > thresholdValue) ? 1 : 0;
        }
        reportUnits(1);
    }

    QVector<double> fOutside(outWidth * outHeight);
    QVector<double> fInside(outWidth * outHeight);
    for (int i = 0; i < insideMask.size(); ++i) {
        if (insideMask[i]) {
            fOutside[i] = sdf_internal::kInfinity;
            fInside[i] = 0.0;
        } else {
            fOutside[i] = 0.0;
            fInside[i] = sdf_internal::kInfinity;
        }
    }

    QVector<double> distOutsideSq = sdf_internal::distanceTransform2D(
        fOutside,
        outWidth,
        outHeight,
        &cancelRequested,
        reportUnits);
    if (distOutsideSq.isEmpty()) {
        if (cancelRequested.load()) {
            emit canceled();
            return;
        }
        emit failed(tr("Distance transform failed."));
        return;
    }

    QVector<double> distInsideSq = sdf_internal::distanceTransform2D(
        fInside,
        outWidth,
        outHeight,
        &cancelRequested,
        reportUnits);
    if (distInsideSq.isEmpty()) {
        if (cancelRequested.load()) {
            emit canceled();
            return;
        }
        emit failed(tr("Distance transform failed."));
        return;
    }

    QVector<double> signedDistances(outWidth * outHeight);
    double minDistance = std::numeric_limits<double>::max();
    double maxDistanceValue = std::numeric_limits<double>::lowest();

    for (int y = 0; y < outHeight; ++y) {
        if (cancelRequested.load()) {
            emit canceled();
            return;
        }
        const int rowOffset = y * outWidth;
        for (int x = 0; x < outWidth; ++x) {
            const int i = rowOffset + x;
            double dist = 0.0;
            if (insideMask[i]) {
                dist = std::sqrt(distOutsideSq[i]);
            } else {
                dist = -std::sqrt(distInsideSq[i]);
            }

            if (maxDistance > 0) {
                if (dist > maxDistance) {
                    dist = maxDistance;
                } else if (dist < -maxDistance) {
                    dist = -maxDistance;
                }
            }

            signedDistances[i] = dist;
            if (dist < minDistance) {
                minDistance = dist;
            }
            if (dist > maxDistanceValue) {
                maxDistanceValue = dist;
            }
        }
        reportUnits(1);
    }

    QImage output(outWidth, outHeight, QImage::Format_Grayscale8);
    const double denom = maxDistanceValue - minDistance;
    for (int y = 0; y < outHeight; ++y) {
        if (cancelRequested.load()) {
            emit canceled();
            return;
        }
        uchar *line = output.scanLine(y);
        const int rowOffset = y * outWidth;
        for (int x = 0; x < outWidth; ++x) {
            const int idx = rowOffset + x;
            double normalized = 0.5;
            if (denom > 0.0) {
                normalized = (signedDistances[idx] - minDistance) / denom;
            }
            const int value = 255 - qBound(0, static_cast<int>(std::lround(normalized * 255.0)), 255);
            line[x] = static_cast<uchar>(value);
        }
        reportUnits(1);
    }

    emit progress(100);
    emit finished(output);
}
