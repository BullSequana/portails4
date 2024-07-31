#!/usr/bin/bash

hello() {
	./examples/hello/server 1 >resultserver1.txt &
	./examples/hello/client 1 Hello,world! >resultclient1.txt
	diff <(grep -c 'fail=OK' ../tests/verifserver1.txt) <(grep -c 'OK' resultserver1.txt)
	diff <(grep -c 'fail=OK' ../tests/verifclient1.txt) <(grep -c 'OK' resultclient1.txt)
	grep -q "Hello,world!" resultserver1.txt
}
hello
