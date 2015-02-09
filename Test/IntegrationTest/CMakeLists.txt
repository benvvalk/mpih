add_test(HelloWorldTest
	${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 10
	${MPIEXEC_PREFLAGS}
	mpih ${MPIEXEC_POSTFLAGS}
	run ${CMAKE_CURRENT_SOURCE_DIR}/hello-world.sh
)

add_test(TandemSendTest
	${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 2
	${MPIEXEC_PREFLAGS}
	mpih ${MPIEXEC_POSTFLAGS}
	run ${CMAKE_CURRENT_SOURCE_DIR}/tandem-send-test.sh 8k
)

set_tests_properties(
	HelloWorldTest
	TandemSendTest
	PROPERTIES ENVIRONMENT
	"PATH=${PROJECT_BINARY_DIR}:$ENV{PATH}"
)
