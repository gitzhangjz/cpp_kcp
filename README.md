# KCP协议的C++版本

## 完成测试5次，三个模式的平均rtt分别为:507ms、212ms、189ms（与本地机器运行速度相关)：

![image](https://github.com/gitzhangjz/cpp_kcp/blob/main/img/mode0.png)

![image](https://github.com/gitzhangjz/cpp_kcp/blob/main/img/mode1.png)

![image](https://github.com/gitzhangjz/cpp_kcp/blob/main/img/mode2.png)

## KCP协议主要流程

![image](https://github.com/gitzhangjz/cpp_kcp/blob/main/img/process.png)

## 与官方C语言版本主要区别：

1. 使用部分C++11特性；
2. 面向对象实现；
3. `KCPCB::snd_queue`、`KCPCB::rcv_queue`、`KCPCB::snd_buf`、`KCPCB::rcv_buf`，使用 `std::list`实现，`KCPCB::acklist`使用 `std::vector`实现。使用标准库的 `list`和 `vector`接口，代码更简洁（全部代码+注释只有不到1200行），效率几乎没有差别（使用官方测试代码）；
