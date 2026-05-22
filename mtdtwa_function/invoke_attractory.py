#!/usr/bin/env python3
import subprocess, json, os

def run_attractor(formula: str,
                  acc_marks: list[int],
                  ctrl_vars: list[str],
                  print_output_dir: str = None,
                  exe: str = "./mtdtwa_attr_lazy") -> list[bool]:
    cmd = [
        exe,
        formula,
        ",".join(str(m) for m in acc_marks),
        ",".join(ctrl_vars),
    ]
    if print_output_dir is not None:
        cmd.append(print_output_dir)
        print(cmd)
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)

    # Split into lines, drop empties, take last
    lines = [l for l in proc.stdout.splitlines() if l.strip()]
    if not lines:
        raise RuntimeError("No output from attractor executable")
    json_line = lines[-1]
    return json.loads(json_line)


def run_test_1():
    # Reachability / Büchi
    wins = run_attractor("F a", [0], ["a"],
                         print_output_dir="mtdtwa_figure/test_1.png")
    print("Test-1 winning vector:", wins)

def run_test_2():
    # Safety / co-Büchi
    wins = run_attractor("G(!a)", [0], ["a"],
                         print_output_dir="mtdtwa_figure/test_2.png")
    print("Test-2 winning vector:", wins)

def run_test_3():
    """
    Request-Grant Streett:
      LTL  : G(req -> F grant)
      ACC  : one Streett pair 〈E, F〉
      Sets : 0 = ReqWaiting (E), 1 = ReqServed (F)
    """
    wins = run_attractor(
        "G(req -> F grant)",
        [0, 1],               # Streett pair indices
        ["grant"],
        print_output_dir="mtdtwa_figure/test_3.png",
    )
    print("Test-3 winning vector:", wins)

def run_test_4():
    """
    Two-fair-turns: Generalised Büchi with two accepting sets
      LTL  : G F turnC  &  G F turnE
      ACC  : set-0 = qC,  set-1 = qE
    """
    wins = run_attractor(
        "G F turnC & G F turnE",
        [0, 1],               # both GBüchi sets must be visited
        ["turnC"],
        print_output_dir="mtdtwa_figure/test_4.png",
    )
    print("Test-4 winning vector:", wins)

def run_test_5():
    """
    Rabin(1) :  avoid 'bad' infinitely AND visit 'goal' infinitely
    LTL      :  G F goal  &  G(!bad)
    enc      :  one Rabin pair ⟨E=bad,  F=goal⟩  → exporter numbers them 0,1
    EXPECT   :  **mixed**
                • all states reachable without hitting  'bad'  are  True
                • the rejecting sink that loops on    'bad'   is  False
    """
    wins = run_attractor(
        "G F goal & G(!bad)",
        [0, 1],                 # Rabin pair indices
        ["goal"],               # controller decides when to raise 'goal'
        "mtdtwa_figure/test_5.png",
    )
    print("Test-5 wins:", wins)

def run_test_6():
    """
    Double-Streett (two independent request/response channels)
      LTL :  G(req1 -> F grant1)  &  G(req2 -> F grant2)
      ACC :  ⟨E1=req1_wait , F1=grant1_done⟩  +  ⟨E2=req2_wait , F2=grant2_done⟩
             exporter order → 0,1,2,3
      EXPECT : **mixed**
               • initial state (neither request pending)  → True
               • states where either request is already pending and cannot
                 be granted due to uncontrollable vars      → False
    """
    wins = run_attractor(
        "G(req1 -> F grant1) & G(req2 -> F grant2)",
        [0, 1, 2, 3],           # E1,F1,E2,F2
        ["grant1", "grant2"],
        "mtdtwa_figure/test_6.png",
    )
    print("Test-6 wins:", wins)

def run_test_7():
    """
    Triple-fair-turn GBüchi
      LTL : G F turnA  &  G F turnB  &  G F turnC
      ACC : 3 GBüchi sets (qA,qB,qC) → indices 0,1,2
      Controller only controls 'turnA', the other turns are environmental.
      EXPECT : **mixed** (usually only the subset of states where the
                environment has just yielded B and C control can be winning).
    """
    wins = run_attractor(
        "G F turnA & G F turnB & G F turnC",
        [0, 1, 2],              # three accepting sets
        ["turnA"],
        "mtdtwa_figure/test_7.png",
    )
    print("Test-7 wins:", wins)

def run_test_8():
    """
    3-pair Streett  (triple request/response)
      LTL   :  ∧_{i=1..3}  G(req_i → F grant_i)
      ACC   :  ⟨E1,F1,E2,F2,E3,F3⟩  → indices 0-5
      EXPECT: **mixed**  (states with ungrantable pending request are losing)
    """
    print("T8 wins:", run_attractor(
        "G(reqA -> F grantA) & G(reqB -> F grantB) & G(reqC -> F grantC)",
        list(range(6)),                # 0..5
        ["grantA", "grantB", "grantC"],
        "mtdtwa_figure/test_8.png"))

def run_test_9():
    """
    2-pair Rabin
      LTL   :  (G F g1 & G(!b1))  &  (G F g2 & G(!b2))
      ACC   :  ⟨E1=b1,F1=g1,  E2=b2,F2=g2⟩  → indices 0-3
      EXPECT: **mixed**
    """
    print("T9 wins:", run_attractor(
        "G F g1 & G(!b1) & G F g2 & G(!b2)",
        [0, 1, 2, 3],
        ["g1", "g2"],
        "mtdtwa_figure/test_9.png"))

def run_test_10():
    """
    4-set GBüchi fairness (turn-taking among 4 parties)
      LTL   :  ∧_{p∈{1..4}}  G F turn_p
      ACC   :  sets 0,1,2,3
      Controller can only act on turn1.
      EXPECT: **mixed** (environment can starve some turns from some states)
    """
    print("T10 wins:", run_attractor(
        "G F turn1 & G F turn2 & G F turn3 & G F turn4",
        [0, 1, 2, 3],
        ["turn1"],
        "mtdtwa_figure/test_10.png"))

def run_test_11():
    """
    Safety + double liveness (co-Büchi + 2 Büchi)
      LTL   :  G(!fault)  &  G F hot  &  G(cold -> F warm)
      Typical export order → 0=fault(co-B), 1=hot(B), 2=warm(B)
      ACC   :  [0,1,2]
      EXPECT: **mixed**  (any state stuck in fault is losing)
    """
    print("T11 wins:", run_attractor(
        "G(!fault) & G F hot & G(cold -> F warm)",
        [0, 1, 2],
        ["hot", "warm"],
        "mtdtwa_figure/test_11.png"))

def run_test_12():
    """
    5-pair Streett  (large GR(1)-style spec)
      LTL   :  ∧_{i=1..5}  G(req_i -> F grant_i)
      ACC   :  ⟨E1,F1,…,E5,F5⟩ → indices 0-9
      EXPECT: **mixed** (lots of losing combos)
    """
    print("T12 wins:", run_attractor(
        " & ".join(f"G(req{i} -> F grant{i})" for i in range(1, 6)),
        [0,1],               # 0..9
        [f"grant{i}" for i in range(1, 6)],
        "mtdtwa_figure/test_12.png"))


if __name__ == "__main__":
    print("Working directory:", os.getcwd())
    run_test_1()   # reachability
    run_test_2()   # safety
    run_test_3()   # 1-Streett
    run_test_4()   # 2-GBüchi
    run_test_5()   # 1-Rabin
    run_test_6()   # 2-Streett
    run_test_7()   # 3-GBüchi
    run_test_8()   # 3-Streett
    run_test_9()   # 2-Rabin
    run_test_10()  # 4-GBüchi
    run_test_11()  # safety + 2 liveness
    run_test_12()  # 5-Streett
 