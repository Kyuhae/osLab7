 #!/bin/bash
 echo "running testsssss\n"
 rm -f output.txt
 for i in {1..100}; do
    ./follow_test.run > output.txt
 done
 echo "finished follow tests\n"
