#!/usr/bin/bash

get_me() {
	./examples/get_matching/server 1 >resultserver2.txt &
	./examples/get_matching/client 1 >resultclient2.txt
	diff <(grep -c 'fail=OK' ../tests/verifserver2.txt) <(grep -c 'OK' resultserver2.txt)
	diff <(grep -c 'fail=OK' ../tests/verifclient2.txt) <(grep -c 'OK' resultclient2.txt)
}
get_me
