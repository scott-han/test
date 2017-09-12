#!/bin/bash -x

MYDIR_=`dirname ${0}`  
MYDIR=`cd $MYDIR_ && pwd -W`

Bail() {
	echo "----------------------------------------"
	echo ">>> BAIL <<< $(date) FAILED: " ${0} ${*}
	echo "----------------------------------------"
	exit 1
}
export -f Bail

Process_Run_Root_Of_Program() {
	echo "Processing coverage for ${1}"
	cd ${1} || Bail
	#find . -iname "New_Test_Run.*" -exec lcov -c -d {} -o {}.info  \;
	find . -iname "New_Test_Run.*" ! -iname "*.info" -print0 | xargs -0 -I{} -n 1 lcov -b "${MYDIR}" -c -d {} -o {}.info || Bail "Lcov individual test run"
	LCOV_ADDITIONS="$(find . -maxdepth 1 -name "New_Test_Run.*.info" -print0 | xargs -0 -n 1 -I{} echo -a {})"
	if [ ! -z "${LCOV_ADDITIONS}" ] 
	then
		lcov ${LCOV_ADDITIONS} -o Total.info || Bail "Lcov collating runs for given target"
		genhtml -t "Coverager results for ${1}" --legend -o HTML_Report Total.info || Bail "Gen_HTML for given target"
	fi
	echo "Done processing coverage for ${1}"
}
export -f Process_Run_Root_Of_Program

echo "${COVERAGE_ROOT:?Need to set COVERAGE_ROOT to a non-empty value}" > /dev/null

rm -fr ${COVERAGE_ROOT}
mkdir -p ${COVERAGE_ROOT} || Bail

make clean
BUILD_COVERAGE='yes' make ${*} || Bail "Making tests"

cd ${COVERAGE_ROOT} || Bail 

#find . -type d -iname "Transient_Run" -exec bash -c 'Process_Run_Root_Of_Program "${0}"' {} \;
#find . -type d -iname "Transient_Run.*" -print0 | xargs -0 -I {} -n 1 bash -x -c 'Process_Run_Root_Of_Program "${0}" "${1}" ' {} ${MYDIR} || Bail "On iterating Transient_Run dirs"

while read -r Program_Run_Root 
do
	Process_Run_Root_Of_Program "${Program_Run_Root}" || Bail "On iterating test runs for ${Program_Run_Root}"
done < <(find . -type d -iname "New_Test_Run.*" -exec dirname {}	\; | sort | uniq)
