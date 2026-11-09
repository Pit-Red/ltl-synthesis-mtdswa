#pragma once
#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TlsfInfo {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::string ltl;
    std::string semantics;   // raw output of `syfco -s`, e.g. "Moore" or "Strict,Mealy"
    bool mealy_semantics;
};

TlsfInfo extract_tlsf_info(const std::string& tlsfFilePath);

// Reference verdict from ltlsynt, independent of our Mealy/Moore encoding.
//
// ltlsynt 2.15.1 cannot solve Moore games, and `ltlsynt --tlsf` picks the game
// type from the TLSF TARGET field, not SEMANTICS.  For a spec with
// SEMANTICS: Moore / TARGET: Mealy it would therefore solve the unconverted
// formula as a Mealy game, which is the wrong question.  Instead we let syfco
// convert the spec to plain Mealy semantics (`syfco -os mealy`, which
// replaces every input i by X i and encodes Strict into the formula) and
// solve that with ltlsynt.
// The verdict is realizability under the file's SEMANTICS.
enum class LtlsyntVerdict { Realizable, Unrealizable, Timeout, Error };

struct LtlsyntTlsfResult {
    LtlsyntVerdict verdict = LtlsyntVerdict::Error;
    std::string mealy_formula;  // formula after syfco's conversion to Mealy
    std::string raw_output;
};

LtlsyntTlsfResult ltlsynt_tlsf_realizability(const std::string& tlsfFilePath,
                                             const TlsfInfo& info,
                                             unsigned timeout_seconds);

const char* to_string(LtlsyntVerdict v);
