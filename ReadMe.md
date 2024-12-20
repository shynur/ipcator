试运行:

```bash
make run  # 默认使用尽可能新的 C++ 特性
# 或
make run-build  # 更好的性能, 更少的日志
# 或
echo 20 | make try-backport  # 加快编译, 并尝试 -std=c++20
echo 23 | make try-backport  # 同上, 并尝试 -std=c++23
```

见 [用法示例](./include/tester.hpp).
