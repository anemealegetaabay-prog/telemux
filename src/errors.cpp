#include "telemux/errors.h"

namespace telemux {

const char* error_code_name(ErrorCode code) {
    switch (code) {
        case ErrorCode::kOk: return "Ok";
        case ErrorCode::kTruncated: return "Truncated";
        case ErrorCode::kBadMagic: return "BadMagic";
        case ErrorCode::kUnsupportedVersion: return "UnsupportedVersion";
        case ErrorCode::kSectionExceedsParentBudget: return "SectionExceedsParentBudget";
        case ErrorCode::kNestTooDeep: return "NestTooDeep";
        case ErrorCode::kReservedSectionTag: return "ReservedSectionTag";
        case ErrorCode::kSessionUnknown: return "SessionUnknown";
        case ErrorCode::kSessionIdSpaceExhausted: return "SessionIdSpaceExhausted";
        case ErrorCode::kFrameTooLarge: return "FrameTooLarge";
        case ErrorCode::kChecksumMismatch: return "ChecksumMismatch";
        case ErrorCode::kLayoutInvalid: return "LayoutInvalid";
        case ErrorCode::kQuerySyntaxError: return "QuerySyntaxError";
        case ErrorCode::kCalibrationOutOfDomain: return "CalibrationOutOfDomain";
        case ErrorCode::kCalibrationTruncated: return "CalibrationTruncated";
        case ErrorCode::kCalibrationInvalidCurve: return "CalibrationInvalidCurve";
    }
    return "Unknown";
}

}  // namespace telemux
