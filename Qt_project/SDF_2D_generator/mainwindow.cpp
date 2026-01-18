#include "MainWindow.h"
#include "./ui_MainWindow.h"

#include "SdfWorker.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QResizeEvent>
#include <QThread>

namespace {
constexpr int kDefaultMaxDistance = 512;
constexpr int kMaxOutputSize = 8192;
constexpr int kMaxSearchDistance = 2048;
}

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
