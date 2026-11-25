"""Summarize verified duplex runs. RSS is sampled, and CPU is worker process time."""
import csv
import re
import sys
from pathlib import Path

writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(("case","local_clouds","peer_clouds","import_poses","poses_during_fault",
                 "export_worker_cpu_s","import_worker_cpu_s","export_peak_rss_mib","import_peak_rss_mib"))
for case in sorted(Path(sys.argv[1]).iterdir(),key=lambda p:int(p.name.split("-")[0])):
    def usage(names):
        cpu = 0.
        peak = 0
        for name in names:
            log = (case/(name+".log")).read_text()
            stats = re.findall(r"^stats role=\w+ samples=\d+ bytes=\d+ cpu_s=([\d.]+)",log,re.M)
            assert len(stats)==2
            cpu += sum(map(float,stats))
            with (case/(name+"-rss.csv")).open() as stream:
                peak = max(peak,max(int(row["combined_rss_kib"]) for row in csv.DictReader(stream)))
        return round(cpu,3),round(peak/1024,1)

    with (case/"delivery.csv").open() as stream:
        clouds = list(csv.DictReader(stream))
    reader = (case/"reader.log").read_text()
    assert "240/240 nested PoseArrays verified" in reader and len(clouds)==60
    times = list(map(int,re.findall(r"^pose \d+ (\d+)",reader,re.M)))
    during = ""
    if (case/"fault-start").exists():
        start,end = (int((case/("fault-"+side)).read_text()) for side in ("start","end"))
        during = sum(start<t<end for t in times)
        assert during and times[0]<start<end<times[-1]
    names = ["gateway","replacement"] if (case/"replacement.log").exists() else ["gateway"]
    export_cpu,export_rss = usage(names)
    import_cpu,import_rss = usage(["import"])
    writer.writerow((case.name,len(clouds),sum(int(c["peer_recorded"]) for c in clouds),len(times),
                     during,export_cpu,import_cpu,export_rss,import_rss))
