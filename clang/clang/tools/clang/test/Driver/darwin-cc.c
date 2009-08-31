// RUN: clang -ccc-no-clang -ccc-host-triple i386-apple-darwin10 -m32 -### -MD -g -fast -Q -dA -mkernel -ansi -aFOO -S -o /tmp/OUTPUTNAME -g0 -gfull -O2 -Werror -pedantic -Wmost -w -std=c99 -trigraphs -v -pg -fFOO -undef -Qn --param a=b -fmudflap -coverage -save-temps -nostdinc -I ARG0 -F ARG1 -I ARG2 -P -MF ARG3 -MG -MP -remap -g3 -H -D ARG4 -U ARG5 -A ARG6 -D ARG7 -U ARG8 -A ARG9 -include ARG10 -pthread %s 2> %t.log &&
// RUN: grep ' ".*cc1" "-E" "-nostdinc" "-v" "-I" "ARG0" "-F" "ARG1" "-I" "ARG2" "-P" "-MD" "/tmp/OUTPUTNAME.d" "-MF" "ARG3" "-MG" "-MP" "-MQ" "/tmp/OUTPUTNAME" "-remap" "-dD" "-H" "-D__STATIC__" "-D_REENTRANT" "-D" "ARG4" "-U" "ARG5" "-A" "ARG6" "-D" "ARG7" "-U" "ARG8" "-A" "ARG9" "-include" "ARG10" ".*darwin-cc.c" "-D_MUDFLAP" "-include" "mf-runtime.h" "-mmacosx-version-min=10.6.0" "-m32" "-mkernel" "-mtune=core2" "-ansi" "-std=c99" "-trigraphs" "-Werror" "-pedantic" "-Wmost" "-w" "-fast" "-fno-eliminate-unused-debug-symbols" "-fFOO" "-fmudflap" "-O2" "-undef" "-fpch-preprocess" "-o" ".*darwin-cc.i"' %t.log &&
// RUN: grep ' ".*cc1" "-fpreprocessed" ".*darwin-cc.i" "-O3" "-dumpbase" ".*darwin-cc.c" "-dA" "-mmacosx-version-min=10.6.0" "-m32" "-mkernel" "-mtune=core2" "-ansi" "-aFOO" "-auxbase-strip" "/tmp/OUTPUTNAME" "-g" "-g0" "-g" "-g3" "-O2" "-Werror" "-pedantic" "-Wmost" "-w" "-ansi" "-std=c99" "-trigraphs" "-version" "-p" "-fast" "-fno-eliminate-unused-debug-symbols" "-fFOO" "-fmudflap" "-undef" "-fno-ident" "-o" "/tmp/OUTPUTNAME" "--param" "a=b" "-fno-builtin" "-fno-merge-constants" "-fprofile-arcs" "-ftest-coverage"' %t.log &&

// RUN: true

