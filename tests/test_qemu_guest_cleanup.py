#!/usr/bin/env python3
"""A failed QMP greeting/negotiation must reap the owned VM and its handles."""
import io,pathlib,subprocess,sys,tempfile,unittest
from unittest.mock import patch
from qemu_platform import Guest
class BrokenMonitor:
 def __init__(self,fail_at):self.fail_at=fail_at;self.reads=0;self.closed=False
 def readline(self):
  self.reads+=1
  if self.reads==self.fail_at:raise TimeoutError('fixture QMP timeout')
  return b'{"QMP":{}}\n'
 def write(self,data):return len(data)
 def close(self):self.closed=True
class MonitorSocket:
 def __init__(self,stream):self.stream=stream;self.closed=False
 def settimeout(self,seconds):assert seconds==3
 def connect(self,path):pass
 def makefile(self,*a,**kw):return self.stream
 def close(self):self.closed=True
class FailedGuestCleanup(unittest.TestCase):
 def test_greeting_and_capabilities_failures_reap_owned_child(self):
  for stage in (1,2):
   with self.subTest(stage=stage),tempfile.TemporaryDirectory() as directory:
    guest=Guest.__new__(Guest);guest.sockpath=pathlib.Path(directory)/'qmp.sock';guest.sockpath.touch()
    guest.events=[];guest.logfile=io.BytesIO()
    guest.process=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    stream=BrokenMonitor(stage);monitor=MonitorSocket(stream)
    try:
     with patch('qemu_platform.socket.socket',return_value=monitor):
      with self.assertRaisesRegex(TimeoutError,'fixture QMP timeout'):guest._connect_monitor()
     self.assertIsNotNone(guest.process.poll())
     self.assertTrue(stream.closed and monitor.closed and guest.logfile.closed and guest.process.stdin.closed)
     guest.close() # Cleanup is also safe when the VM and all handles are closed.
    finally:
     if guest.process.poll() is None:guest.process.kill()
     guest.process.wait()
if __name__=='__main__':unittest.main()
