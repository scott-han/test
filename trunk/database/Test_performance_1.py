import sys
import os
import time
import datetime
import glob
from os.path import basename

def GetNumber(line, prefix, suffix):
	idx = line.find(prefix);
	if idx > 0:
		start = idx + len(prefix)
		end = line.find(suffix, start)
		if end > 0:
			temp = int(line[start:end])
			return temp;
	
	return 0;
	
def Average(l, drop = True):
	l.sort();
	length = len(l);
	if (length == 0):
		return 0;
	length = length / 4;
	sum = 0
	count = 0;
	start = 0;
	end = len(l)
	if drop:
		start = length;
		end = len(l) - length;
	for a in l[start:end]:
		sum += a;
		count += 1;
	
	return sum / count;
	
def GetThroughPut(output, fName, msg_size, type):
	f = open(fName, "r")
	lines = f.readlines();
	all = [];
	for line in lines:
		read = 0
		write = 0
		temp = GetNumber(line, " read Hz(=", ")")
		if temp > 0:
			read = temp;
		
		temp = GetNumber(line, "write Hz(=", ")")
		if (temp > 0):
			write = temp;
		if (read > 0 or write > 0):
			start = line.find("local=")
			if start >= 0:
				start += 6;
				utc = line[start:start+27]
				unixtime = int(time.mktime(datetime.datetime.strptime(utc, "%Y-%b-%d %H:%M:%S.%f").timetuple()))
				all.append(msg_size + "," + type + "," + str(write) + "," + str(read) + "," + str(unixtime) + "\n");
	
	all = all[2:len(all) - 2]
	for x in all:
		output.write(x);
	
def GetPerformance(output, fName, msg_size, type):
	f = open(fName, "r")
	lines = f.readlines();
	for line in lines:
		if (len(line) <= 0):
			continue;
		temp = line.split(",")
		output.write(msg_size + "," + type + "," + temp[0].strip(' \n') + "," + temp[1].strip(' \n') + "," + temp[2].strip(' \n') + "," + temp[3].strip(' \n') + "," + temp[4].strip(' \n') + "\n");

def DetectLatency(line):
	idx = line.find("PERFORMANCE LOG: ")
	if (idx > 0):
		start = idx + len("PERFORMANCE LOG: ")
		end = line.find("has", start);
		if (end > 0):
			return None, None;
		content = line[start:-1]
		temp = content.split(",");
		return temp[0].strip(' \n'), temp[1].strip(' \n')
	return None, None;

def GetLatency(output, fName, msg_size, type):
	f = open(fName, "r")
	for line in f.readlines():
		if (len(line) <= 0):
			continue;
		seq, latency=DetectLatency(line);
		if seq == None:
			continue;
		output.write(msg_size + "," + type + "," + seq + "," + latency + "\n");
	
def Process1Pub(f, p, latency, msg_size, clients):
	try:
		fSuffix = str(msg_size) + "_1pub.txt"
		GetThroughPut(f, "Synapse_" + fSuffix, msg_size, "1pub")
		GetPerformance(p, "perf_" + fSuffix, msg_size, "1pub")
		GetLatency(latency, "latency_" + fSuffix, msg_size, "1pub")
	except Exception, e:
		pass

def ProcessMPub(f, p, latency, msg_size, clients):
	try:
		fSuffix = str(msg_size) + "_" + str(clients) + "pub.txt"
		GetThroughPut(f, "Synapse_" + fSuffix, msg_size, "Mpub");
		GetPerformance(p, "perf_" + fSuffix, msg_size, "Mpub")
		GetLatency(latency, "latency_" + fSuffix, msg_size, "Mpub")
	except Exception, e:
		pass

def Process1Pub1Sub(f, p, latency, msg_size, clients):
	try:
		fSuffix = str(msg_size) + "_1pub1sub.txt"
		GetThroughPut(f, "Synapse_" + fSuffix, msg_size, "1pub1sub");
		GetPerformance(p, "perf_" + fSuffix, msg_size, "1pub1sub")
		GetLatency(latency, "latency_" + fSuffix, msg_size, "1pub1sub")
	except Exception, e:
		pass

def ProcessMPubMSub(f, p, latency, msg_size, clients):
	try:
		fSuffix = str(msg_size) + "_" + str(clients) + "pub" + str(clients) + "sub.txt"
		GetThroughPut(f, "Synapse_" + fSuffix, msg_size, "MpubMsub");
		GetPerformance(p, "perf_" + fSuffix, msg_size, "MpubMsub")
		GetLatency(latency, "latency_" + fSuffix, msg_size, "MpubMsub")
	except Exception, e:
		pass
	
def ParseFile(f, p, latency, msg_size, clients):
	Process1Pub(f, p, latency, msg_size, clients)
	ProcessMPub(f, p, latency, msg_size, clients)
	Process1Pub1Sub(f, p, latency, msg_size, clients)
	ProcessMPubMSub(f, p, latency, msg_size, clients)

def ParseMessageRateLatency(f):
	try:
		for file in glob.glob("./mrlatency*.txt"):
			name = basename(file)
			sections=name.split('_')
			msg_size=sections[1]
			type=sections[3]
			temp=open(file, "r")
			for line in temp.readlines():
				if (len(line) <= 0):
					continue;
				seq, latency=DetectLatency(line);
				if seq == None:
					continue;
				f.write(msg_size + "," + type + "," + seq + "," + latency + "\n")
	except Exception, e:
		pass;

def ParseMessageRateThroughput(f):
	try:
		for file in glob.glob("./mrSynapse*.txt"):
			name = basename(file)
			sections = name.split('_')
			msg_size = sections[1]
			type = sections[2]
			GetThroughPut(f, file, msg_size, type);
	except Exception, e:
		pass;
	
if __name__ == '__main__':
	f = open("result.csv", "w")
	f.write("Msg_size,Type,Write,Read,Time\n")
	p = open("perf.csv", "w")
	p.write("Msg_size,Type,CPU,Mem,DiskIO,Network,Time\n");
	latency=open("latency.csv", "w")
	latency.write("Msg_size,Type,Sequence,latency\n");
	for i in range(2, len(sys.argv)):
		ParseFile(f, p, latency, sys.argv[i], sys.argv[1])
	f.close()
	p.close()
	latency.close()
	latency = open("mrlatency.csv", "w")
	latency.write("Msg_size,Type,Sequence,latency\n");
	ParseMessageRateLatency(latency);
	latency.close()
	f = open("throughput.csv", "w")
	f.write("Msg_size,Type,Write,Read,Time")
	ParseMessageRateThroughput(f)
	f.close()