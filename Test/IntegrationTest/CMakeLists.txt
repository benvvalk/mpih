add_test(TandemSendTest
	${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 2
	${MPIEXEC_PREFLAGS}
	mpih ${MPIEXEC_POSTFLAGS}
	run ${CMAKE_CURRENT_SOURCE_DIR}/tandem-send-test.sh 8k
)

set_tests_properties(
	TandemSendTest PROPERTIES ENVIRONMENT
	"PATH=${PROJECT_BINARY_DIR}:$ENV{PATH}"
)
