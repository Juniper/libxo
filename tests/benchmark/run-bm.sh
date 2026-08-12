#!/bin/sh

if [ -z "${BIN}" -o -z "${BM_TEST}" -o -z "${SAMPLE}" ]; then
    echo missing variables
    exit 1
fi

${BIN} ${BM_ARGS} > out/${BM_TEST}.out 2> out/${BM_TEST}.err &

BM_PID=$!
echo PID is $BM_PID

sleep 0.3
${SAMPLE} $BM_PID 8 ${SAMPLE_ARGS} -f out/${BM_TEST}.sample 2>/dev/null

wait $BM_PID
echo "done"

exit 0
