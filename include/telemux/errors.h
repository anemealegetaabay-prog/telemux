#pragma once

#include <cstdint>
#include <string>

namespace telemux {

enum class ErrorCode : uint32_t {
    kOk = 0,
    kTruncated,
    kBadMagic,
    kUnsupportedVersion,
    kSectionExceedsParentBudget,
    kNestTooDeep,
    kReservedSectionTag,
    kSessionUnknown,
    kSessionIdSpaceExhausted,
    kFrameTooLarge,
    kChecksumMismatch,
    kLayoutInvalid,
    kQuerySyntaxError,
    kCalibrationOutOfDomain,
    kCalibrationTruncated,
    kCalibrationInvalidCurve,
    kContainerBadMagic,
    kContainerUnsupportedVersion,
    kContainerTruncated,
    kContainerChecksumMismatch,
    kContainerIndexInvalid,
    kContainerIoError,
    kAlertRuleSyntaxError,
    kClockInsufficientSamples,
    kRegistryDuplicateDevice,
    kRegistryUnknownDevice,
    kRegistryTruncated,
    kRegistryInvalidDescriptor,
    kExportUnsupportedFormat,
    kExportEncodingError,
    kExportParseError,
    kTraceQuerySyntaxError,
    kDigestInsufficientData,
    kProfileSyntaxError,
    kProfileUnresolvedReference,
    kProfileDuplicateSection,
};

struct Error {
    ErrorCode code = ErrorCode::kOk;
    std::string detail;

    bool ok() const { return code == ErrorCode::kOk; }
};

inline Error make_error(ErrorCode code, std::string detail = {}) {
    return Error{code, std::move(detail)};
}

const char* error_code_name(ErrorCode code);

// Minimal Result<T>: either holds a value or an Error. Deliberately not a
// full std::variant wrapper -- callers in this codebase always check ok()
// before touching value().
template <typename T>
class Result {
public:
    Result(T value) : has_value_(true), value_(std::move(value)) {}
    Result(Error error) : has_value_(false), error_(std::move(error)) {}

    bool ok() const { return has_value_; }
    const T& value() const { return value_; }
    T& value() { return value_; }
    const Error& error() const { return error_; }

private:
    bool has_value_;
    T value_{};
    Error error_{};
};

}  // namespace telemux
