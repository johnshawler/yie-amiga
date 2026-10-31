ps -o pid,pcpu,etime,time,nlwp,cmd -C fs-uae | cut -c1-120; top -b -n1 -H | head -20 | cut -c1-110
