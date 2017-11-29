 #!/bin/bash
 echo "running testsssss\n"
 rm -f output.txt
 for i in {1..100}; do
    exec `./stress_test.run -k 100 -n 100 -s` >> output.txt
 done
 echo "finished follow tests\n"
