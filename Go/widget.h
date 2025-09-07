#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QGraphicsPixmapItem> //图形元素
#include <QGraphicsView> //视图
#include <QGraphicsScene> //场景
#include <player.h>
#include <QTimer>

namespace Ui {
class Widget;
}

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    void keyPressEvent(QKeyEvent* event);
    void keyReleaseEvent(QKeyEvent* event);
    void move();
    void pmove(int dir);
    void change();
    void jump();
    void gravity();
    ~Widget();

private:
    Ui::Widget *ui;
    QGraphicsView mview;
    QGraphicsScene mscene;
    QGraphicsPixmapItem bg;
    QGraphicsPixmapItem road;
    QList<int> mKeyList;
    QTimer* mtimer;
    QTimer* m2timer;
    QTimer* timer;
    QTimer* gtimer;
    player hero;

    int mp=1;
    int jn=0;
    int drop=0;
    bool j=true;
    double g=60;
    double t=0;
};

#endif // WIDGET_H
