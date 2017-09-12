#!/bin/sh -x

MYDIR=`dirname ${0}`

bail() {
	exit 1
}

cd ${MYDIR} || bail

cp "${THRIFTDLLPATH}" bin\\Debug\\ || bail
cp "${RMQDLLPATH}" bin\\Debug\\ || bail

csc //nologo //debug:full //t:exe //out:bin\\Debug\\subscribe.exe //r:"${THRIFTDLLPATH}" //r:"${RMQDLLPATH}" Program.cs ..\\common\\*.cs || bail

