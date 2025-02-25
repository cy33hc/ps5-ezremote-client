#ifndef RANGE_PARSER_HPP
#define RANGE_PARSER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace range_parser {

    /**
     * Defines the unit of the range as bytes.
     */
    constexpr const char *UNIT_BYTES = "bytes";

    /**
     * Generic range exception. Usually thrown when an invalid range is being parsed.
     */
    class RangeException : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    /**
     * Struct defining a single part range.
     */
    struct Range {
        /// The start offset of the range.
        std::uint64_t start;
        /// The length of the range.
        std::uint64_t length;

        Range(std::uint64_t pStart, std::uint64_t pLength)
                : start(pStart), length(pLength) {}

        /**
         * Creates a content range string, suitable to be used in "Content-Range" header.
         *
         * @param pSize the total size of the body which the range refers to.
         * @param pUnit the unit of the range. This is set to "bytes" by default.
         * @return the constructed content range string.
         */
        std::string content_range(std::uint64_t pSize, const char *pUnit = UNIT_BYTES) const;
    };

    /**
     * Struct defining a generic HTTP range.
     * This can be either a single part or a multipart range. Such can be determined by checking
     * the length of \ref HTTPRange.ranges.
     */
    struct HTTPRange {
        /// The HTTP range unit.
        std::string unit;
        /// The parsed HTTP range(s).
        std::vector<Range> ranges;

        /**
         * Computes the total length of the HTTP range.
         * This is the sum of the length of all parsed ranges.
         *
         * @return the total length of the HTTP range.
         */
        std::uint64_t total_length();
    };

    /**
     * Parses the provided HTTP range string.
     *
     * @param pRange the HTTP range string.
     * @param pSize the total size of the body which the range refers to.
     * @return the parsed HTTP range.
     */
    HTTPRange parse(const std::string &pRange, std::uint64_t pSize);

}

#endif //RANGE_PARSER_HPP