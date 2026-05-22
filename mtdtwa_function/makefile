# =============================================================================
# Compiler & flags
# =============================================================================
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I. -MMD -MP
LDFLAGS  := -L/usr/local/lib -L/opt/homebrew/lib
LDLIBS   := -lspot -lbddx
BUILD_DIR := build

# =============================================================================
# Binaries
# =============================================================================
BIN_MAIN        := el_solver     # test.cc          – run EL solver on a formula
BIN_SYNTH       := synth_runner  # test_synth.cc    – random LTL vs ltlsynt validation
BIN_TLSF        := tlsf_runner   # test_tlsf.cc     – solve a single TLSF benchmark
BIN_TLSF_BATCH  := tlsf_batch    # test_tlsf_batch  – solve a folder of TLSF benchmarks

# =============================================================================
# Sources
# =============================================================================
CORE_SRCS := solve_el_aut.cc solve_el_aut_swa.cc HierachicalChecker.cc

SRCS_MAIN       := test.cc                              $(CORE_SRCS)
SRCS_SYNTH      := tests-randltl/test_synth.cc          \
                   tests-randltl/ltlgen.cc               \
                   tests-randltl/ltlsynt.cc              \
                   $(CORE_SRCS)
SRCS_TLSF       := tests-tlsf/test_tlsf.cc              \
                   tests-tlsf/extract_tlsf_info.cc       \
                   $(CORE_SRCS)
SRCS_TLSF_BATCH := tests-tlsf/test_tlsf_batch.cc        \
                   tests-tlsf/extract_tlsf_info.cc       \
                   $(CORE_SRCS)

# =============================================================================
# Object & dependency files (auto-generated under build/)
# =============================================================================
OBJS_MAIN       := $(SRCS_MAIN:%.cc=$(BUILD_DIR)/%.o)
OBJS_SYNTH      := $(SRCS_SYNTH:%.cc=$(BUILD_DIR)/%.o)
OBJS_TLSF       := $(SRCS_TLSF:%.cc=$(BUILD_DIR)/%.o)
OBJS_TLSF_BATCH := $(SRCS_TLSF_BATCH:%.cc=$(BUILD_DIR)/%.o)

DEPS := $(OBJS_MAIN:.o=.d) $(OBJS_SYNTH:.o=.d) $(OBJS_TLSF:.o=.d) $(OBJS_TLSF_BATCH:.o=.d)

# =============================================================================
# Default: build everything
# =============================================================================
.PHONY: all
all: $(BIN_MAIN) $(BIN_SYNTH) $(BIN_TLSF) $(BIN_TLSF_BATCH)

# =============================================================================
# Link rules
# =============================================================================
$(BIN_MAIN): $(OBJS_MAIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_SYNTH): $(OBJS_SYNTH)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_TLSF): $(OBJS_TLSF)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_TLSF_BATCH): $(OBJS_TLSF_BATCH)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

# =============================================================================
# Compile rule (creates build/ subdirectories as needed)
# =============================================================================
$(BUILD_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# =============================================================================
# Run targets
#
#   make run    FORMULA="GF a" CONTR_VRB="a"
#     → el_solver takes a single LTL formula; FORMULA is quoted so spaces work.
#
#   make synth  ARGS="r1,r2 g1,g2 42 20 15"
#     → synth_runner <inputs> <outputs> <seed> <n_samples> [tree_size]
#
#   make tlsf        ARGS="tests-tlsf/testing_data/selection-ltl-2025/arbiter_2.tlsf"
#   make tlsf-batch  ARGS="tests-tlsf/testing_data/selection-ltl-2025/"
# =============================================================================
.PHONY: run run-twa run-swa synth synth-twa synth-swa tlsf tlsf-batch

run: $(BIN_MAIN)
	./$(BIN_MAIN) "$(FORMULA)" "$(CONTR_VRB)"

run-twa: $(BIN_MAIN)
	EL_SOLVER=twa ./$(BIN_MAIN) "$(FORMULA)" "$(CONTR_VRB)"

run-swa: $(BIN_MAIN)
	EL_SOLVER=swa ./$(BIN_MAIN) "$(FORMULA)" "$(CONTR_VRB)"

synth: $(BIN_SYNTH)
	./$(BIN_SYNTH) $(ARGS)

synth-twa: $(BIN_SYNTH)
	EL_SOLVER=twa ./$(BIN_SYNTH) $(ARGS)

synth-swa: $(BIN_SYNTH)
	EL_SOLVER=swa ./$(BIN_SYNTH) $(ARGS)

tlsf: $(BIN_TLSF)
	./$(BIN_TLSF) $(ARGS)

tlsf-batch: $(BIN_TLSF_BATCH)
	./$(BIN_TLSF_BATCH) $(ARGS)

# =============================================================================
# Debug build  (rebuilds all objects with -g -O0)
# =============================================================================
.PHONY: debug
debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0 -I. -MMD -MP
debug: clean all

# =============================================================================
# Sanitizer build  — catches memory bugs with exact crash location
#   make sanitize
#   then re-run the failing command, e.g.:
#   ./synth_runner r1,r2 g1,g2 42 20
# =============================================================================
.PHONY: sanitize
sanitize: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O1 -I. -MMD -MP \
                      -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS  := -L/usr/local/lib -L/opt/homebrew/lib \
                      -fsanitize=address,undefined
sanitize: clean all

# =============================================================================
# Clean
# =============================================================================
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(BIN_MAIN) $(BIN_SYNTH) $(BIN_TLSF) $(BIN_TLSF_BATCH)

-include $(DEPS)
