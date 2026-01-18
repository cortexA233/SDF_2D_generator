#ifndef SDFWORKER_H
#define SDFWORKER_H

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>

class SdfWorker : public QObject
{
    Q_OBJECT

public:
    SdfWorker(const QImage &input, int outW, int outH, int threshold, int maxDist);

    void requestCancel();

public slots:
    void process();

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

#endif // SDFWORKER_H
