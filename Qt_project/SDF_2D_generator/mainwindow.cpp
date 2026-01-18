#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <atomic>
#include <cmath>
#include <functional>
#include <limits>

#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QResizeEvent>
#include <QThread>
#include <QVector>

namespace sdf_internal {
constexpr double kInfinity = 1e20;

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
    const std::function<void()> &stepFn)
{
    if (width <= 0 || height <= 0 || f.size() != width * height) {
        return {};
    }

    QVector<double> tmp(width * height);
    QVector<double> out(width * height);

    QVector<double> rowIn(width);
    QVector<double> rowOut(width);
    for (int y = 0; y < height; ++y) {
        if (cancel && cancel->load()) {
            return {};
        }
        const int rowOffset = y * width;
        for (int x = 0; x < width; ++x) {
            rowIn[x] = f[rowOffset + x];
        }
        distanceTransform1D(rowIn, rowOut, width);
        for (int x = 0; x < width; ++x) {
            tmp[rowOffset + x] = rowOut[x];
        }
        if (stepFn) {
            stepFn();
        }
    }

    QVector<double> colIn(height);
    QVector<double> colOut(height);
    for (int x = 0; x < width; ++x) {
        if (cancel && cancel->load()) {
            return {};
        }
        for (int y = 0; y < height; ++y) {
            colIn[y] = tmp[y * width + x];
        }
        distanceTransform1D(colIn, colOut, height);
        for (int y = 0; y < height; ++y) {
            out[y * width + x] = colOut[y];
        }
        if (stepFn) {
            stepFn();
        }
    }

    return out;
}
} // namespace sdf_internal

namespace {
constexpr int kDefaultMaxDistance = 512;
constexpr int kMaxOutputSize = 8192;
constexpr int kMaxSearchDistance = 2048;
} // namespace

class SdfWorker : public QObject
{
    Q_OBJECT

public:
    SdfWorker(const QImage &input, int outW, int outH, int threshold, int maxDist)
        : inputImage(input)
        , outWidth(outW)
        , outHeight(outH)
        , thresholdValue(threshold)
        , maxDistance(maxDist)
        , cancelRequested(false)
    {
    }

public slots:
    void process()
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

        const int totalSteps = outHeight + (4 * (outHeight + outWidth)) + outHeight;
        int steps = 0;
        auto step = [&]() {
            if (totalSteps <= 0) {
                return;
            }
            ++steps;
            const int pct = (steps * 100) / totalSteps;
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
            step();
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
            step);
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
            step);
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

        for (int i = 0; i < signedDistances.size(); ++i) {
            if (cancelRequested.load()) {
                emit canceled();
                return;
            }
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

        QImage output(outWidth, outHeight, QImage::Format_Grayscale8);
        const double denom = maxDistanceValue - minDistance;
        for (int y = 0; y < outHeight; ++y) {
            if (cancelRequested.load()) {
                emit canceled();
                return;
            }
            uchar *line = output.scanLine(y);
            for (int x = 0; x < outWidth; ++x) {
                const int idx = x + y * outWidth;
                double normalized = 0.5;
                if (denom > 0.0) {
                    normalized = (signedDistances[idx] - minDistance) / denom;
                }
                const int value = 255 - qBound(0, static_cast<int>(std::lround(normalized * 255.0)), 255);
                line[x] = static_cast<uchar>(value);
            }
            step();
        }

        emit progress(100);
        emit finished(output);
    }

    void requestCancel()
    {
        cancelRequested.store(true);
    }

signals:
    void progress(int value);
    void finished(const QImage &result);
    void canceled();
    void failed(const QString &message);

private:
    QImage inputImage;
    int outWidth;
    int outHeight;
    int thresholdValue;
    int maxDistance;
    std::atomic_bool cancelRequested;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , workerThread(nullptr)
    , worker(nullptr)
{
    ui->setupUi(this);

    ui->outputWidthSpin->setRange(1, kMaxOutputSize);
    ui->outputHeightSpin->setRange(1, kMaxOutputSize);
    ui->thresholdSpin->setRange(0, 255);
    ui->thresholdSpin->setValue(127);
    ui->maxDistanceSpin->setRange(1, kMaxSearchDistance);
    ui->maxDistanceSpin->setValue(kDefaultMaxDistance);

    ui->generateButton->setEnabled(false);
    ui->cancelButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->generateButton, &QPushButton::clicked, this, &MainWindow::onGenerateClicked);
    connect(ui->cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    connect(ui->saveButton, &QPushButton::clicked, this, &MainWindow::onSaveClicked);
}

MainWindow::~MainWindow()
{
    if (worker) {
        worker->requestCancel();
    }
    if (workerThread) {
        workerThread->quit();
        workerThread->wait(1500);
    }
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewLabels();
}

void MainWindow::onBrowseClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Image"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));

    if (filePath.isEmpty()) {
        return;
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Open Image"), tr("Failed to read image file."));
        return;
    }

    originalImage = image;
    originalPath = filePath;
    ui->inputLineEdit->setText(filePath);
    ui->outputWidthSpin->setValue(originalImage.width());
    ui->outputHeightSpin->setValue(originalImage.height());

    sdfImage = QImage();
    ui->generateButton->setEnabled(true);
    ui->saveButton->setEnabled(false);
    ui->progressBar->setValue(0);

    updatePreviewLabels();
}

void MainWindow::onGenerateClicked()
{
    if (originalImage.isNull()) {
        QMessageBox::information(this, tr("Generate SDF"), tr("Please load an image first."));
        return;
    }

    if (workerThread) {
        QMessageBox::information(this, tr("Generate SDF"), tr("SDF generation is already running."));
        return;
    }

    const int outW = ui->outputWidthSpin->value();
    const int outH = ui->outputHeightSpin->value();
    const int threshold = ui->thresholdSpin->value();
    const int maxDist = ui->maxDistanceSpin->value();

    ui->generateButton->setEnabled(false);
    ui->cancelButton->setEnabled(true);
    ui->saveButton->setEnabled(false);
    ui->progressBar->setValue(0);

    workerThread = new QThread(this);
    worker = new SdfWorker(originalImage, outW, outH, threshold, maxDist);
    worker->moveToThread(workerThread);

    connect(workerThread, &QThread::started, worker, &SdfWorker::process);
    connect(worker, &SdfWorker::progress, this, &MainWindow::onSdfProgress);
    connect(worker, &SdfWorker::finished, this, &MainWindow::onSdfFinished);
    connect(worker, &SdfWorker::canceled, this, &MainWindow::onSdfCanceled);
    connect(worker, &SdfWorker::failed, this, &MainWindow::onSdfFailed);

    connect(worker, &SdfWorker::finished, workerThread, &QThread::quit);
    connect(worker, &SdfWorker::canceled, workerThread, &QThread::quit);
    connect(worker, &SdfWorker::failed, workerThread, &QThread::quit);

    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    connect(workerThread, &QThread::finished, this, [this]() {
        workerThread = nullptr;
        worker = nullptr;
    });

    workerThread->start();
}

void MainWindow::onCancelClicked()
{
    if (worker) {
        worker->requestCancel();
        ui->cancelButton->setEnabled(false);
        ui->statusbar->showMessage(tr("Canceling..."), 2000);
    }
}

void MainWindow::onSaveClicked()
{
    if (sdfImage.isNull()) {
        QMessageBox::information(this, tr("Save SDF"), tr("No SDF image to save."));
        return;
    }

    QString suggestedPath;
    if (!originalPath.isEmpty()) {
        const QFileInfo info(originalPath);
        suggestedPath = info.absolutePath() + "/" + info.completeBaseName() + "_sdf.png";
    }

    QString savePath = QFileDialog::getSaveFileName(
        this,
        tr("Save SDF"),
        suggestedPath,
        tr("PNG Image (*.png)"));

    if (savePath.isEmpty()) {
        return;
    }

    if (!savePath.endsWith(".png", Qt::CaseInsensitive)) {
        savePath += ".png";
    }

    if (!sdfImage.save(savePath, "PNG")) {
        QMessageBox::warning(this, tr("Save SDF"), tr("Failed to save image."));
        return;
    }

    ui->statusbar->showMessage(tr("Saved to %1").arg(savePath), 3000);
}

void MainWindow::onSdfProgress(int value)
{
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(value);
}

void MainWindow::onSdfFinished(const QImage &result)
{
    ui->progressBar->setValue(100);
    ui->generateButton->setEnabled(true);
    ui->cancelButton->setEnabled(false);

    if (result.isNull()) {
        QMessageBox::warning(this, tr("Generate SDF"), tr("SDF generation failed."));
        return;
    }

    sdfImage = result;
    ui->saveButton->setEnabled(true);
    updatePreviewLabels();
}

void MainWindow::onSdfCanceled()
{
    ui->progressBar->setValue(0);
    ui->generateButton->setEnabled(true);
    ui->cancelButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->statusbar->showMessage(tr("Canceled."), 2000);
}

void MainWindow::onSdfFailed(const QString &message)
{
    ui->progressBar->setValue(0);
    ui->generateButton->setEnabled(true);
    ui->cancelButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    QMessageBox::warning(this, tr("Generate SDF"), message);
}

void MainWindow::updatePreviewLabels()
{
    const auto setPreview = [](QLabel *label, const QImage &image, const QString &placeholder) {
        if (!label) {
            return;
        }
        if (image.isNull()) {
            label->setPixmap(QPixmap());
            label->setText(placeholder);
            return;
        }
        label->setText(QString());
        const QPixmap pixmap = QPixmap::fromImage(image);
        label->setPixmap(pixmap.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

    setPreview(ui->originalLabel, originalImage, tr("No image loaded"));
    setPreview(ui->sdfLabel, sdfImage, tr("No SDF generated"));
}

#include "mainwindow.moc"
