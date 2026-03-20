#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVector>

class TipsDialog : public QDialog {
    Q_OBJECT

public:
    explicit TipsDialog(QWidget* parent = nullptr);

private slots:
    void showAnother();

private:
    void setupUi();
    void displayTip(int index);
    void updateDots();
    void saveSeen();
    int  pickNext() const;

    // Card content
    QLabel*      m_iconLabel    = nullptr;
    QLabel*      m_titleLabel   = nullptr;
    QLabel*      m_bodyLabel    = nullptr;

    // Bottom bar
    QLabel*      m_counterLabel = nullptr;
    QPushButton* m_anotherBtn   = nullptr;

    // Dot indicators
    QWidget*           m_dotsWidget = nullptr;
    QVector<QLabel*>   m_dots;

    // State
    int          m_currentIndex = -1;
    int          m_tipCount     = 0;
    QVector<int> m_seenEver;    // persisted across sessions in QSettings

    static constexpr int CARD_H = 260;
};
