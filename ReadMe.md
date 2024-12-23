试运行:

```bash
make run  # 默认使用尽可能新的 C++ 特性
# 或
make run-build  # 更好的性能, 更少的日志
# 或
echo 20 | make try-backport  # 仅编译, 测试 C++20 兼容性
echo 23 | make try-backport  # 同上, 测试 C++23
```

见 [用法示例](./include/tester.hpp).
