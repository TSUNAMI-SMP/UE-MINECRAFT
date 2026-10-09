"""Exercise the production queue, including propagation reentrancy and backlog."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class WorkQueue(unittest.TestCase):
    def test_large_backlog_duplicates_reentrant_updates_and_clear(self):
        source = r'''
#include "BridgeWorkQueue.h"
#include <cassert>
#include <functional>
int main() {
    BridgeWorkQueue::UniqueQueue<int,std::hash<int>> q;
    constexpr int count=100000;
    for(int i=0;i<count;++i) {assert(q.Add(i));assert(!q.Add(i));}
    assert(q.Num()==count);
    // Stop at a frame budget; unprocessed work must survive in FIFO order.
    for(int i=0;i<256;++i) assert(q.Pop()==i);
    assert(q.Num()==count-256);
    assert(q.Add(0)); // Self/neighbor propagation may schedule a popped block.
    for(int i=256;i<count;++i) assert(q.Pop()==i);
    assert(q.Pop()==0&&q.IsEmpty());
    for(int pass=0;pass<20;++pass) {
        assert(q.Add(42));assert(!q.Add(42));assert(q.Pop()==42);
    }
    q.Add(99);q.Empty();assert(q.IsEmpty()&&q.Num()==0);
    assert(q.Add(99));assert(q.Pop()==99);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'queue.cpp'
            exe = Path(directory) / 'queue'
            cpp.write_text(source)
            headers = Path(__file__).resolve().parents[1] / 'unreal/UEBridge/Source/UEBridge'
            subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(headers), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
