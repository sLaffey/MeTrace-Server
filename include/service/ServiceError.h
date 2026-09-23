#pragma once

#include <stdexcept>
#include <string>

namespace metrace::service {

/// 业务校验失败（必填缺失、取值越界、枚举非法等）→ HTTP 400。
/// 由 HTTP 层的统一异常处理器转换成 {"error": "..."} 响应。
class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const std::string& message) : std::runtime_error(message) {}
};

/// 目标记录不存在 → HTTP 404。
class NotFoundError : public std::runtime_error {
public:
    explicit NotFoundError(const std::string& message) : std::runtime_error(message) {}
};

} // namespace metrace::service
