#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QImage>
#include <QMainWindow>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QThread;
class SdfWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onBrowseClicked();
    void onGenerateClicked();
    void onCancelClicked();
    void onSaveClicked();
    void onSdfProgress(int value);
    void onSdfFinished(const QImage &result);
    void onSdfCanceled();
    void onSdfFailed(const QString &message);

private:
    void updatePreviewLabels();

    Ui::MainWindow *ui;
    QImage originalImage;
    QImage sdfImage;
    QString originalPath;
    QThread *workerThread;
    SdfWorker *worker;
};
#endif // MAINWINDOW_H
