#!/bin/zsh
# Build Release AU + Standalone, run auval and the unit tests. Usage: tools/validate.sh
cd "${0:A:h}/.." || exit 1
typeset -A res
step() {  # step <name> <cmd...>
    local name=$1; shift
    print -P "%F{blue}==> $name%f"
    if "$@"; then res[$name]=PASS; else res[$name]=FAIL; fi
}

step "configure"        cmake -B build-validate -G Xcode -DWAVEGRID_BUILD_TESTS=ON
step "build Standalone" cmake --build build-validate --config Release --target WaveGrid_Standalone
step "build AU"         cmake --build build-validate --config Release --target WaveGrid_AU
step "build tests"      cmake --build build-validate --config Release --target WaveGrid_Tests
step "auval"            auval -v aumu Wgrd Dpau
t=$(find build-validate -type f -name WaveGrid_Tests -perm +111 | head -1)
if [[ -n $t ]]; then step "unit tests" "$t"; else res[unit tests]=FAIL; fi

print "\n===== SUMMARY ====="
rc=0
for k in configure "build Standalone" "build AU" "build tests" auval "unit tests"; do
    if [[ ${res[$k]} == PASS ]]; then print -P "%F{green}PASS%f  $k"; else print -P "%F{red}FAIL%f  $k"; rc=1; fi
done
(( rc == 0 )) && print -P "%F{green}ALL CHECKS PASSED%f" || print -P "%F{red}VALIDATION FAILED%f"
exit $rc
