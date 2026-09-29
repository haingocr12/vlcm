// Frida agent: bắt named pipe trong tiến trình 32-bit (AutoVLCMO.exe / MctHost.exe).
// Hook KernelBase: CreateFileW, WaitNamedPipeW, CreateNamedPipeW, ConnectNamedPipe,
// ReadFile, WriteFile, GetOverlappedResult, CloseHandle.
// Mỗi gói dữ liệu được gửi về Python bằng send(meta, bytes).

'use strict';

const ALL_PIPES = (typeof ALL_PIPES_FLAG !== 'undefined') ? ALL_PIPES_FLAG : true;

function exp(name) {
  for (const mod of ['kernelbase.dll', 'kernel32.dll']) {
    try {
      const m = Process.getModuleByName(mod);
      const p = m.findExportByName ? m.findExportByName(name) : Module.findExportByName(mod, name);
      if (p) return p;
    } catch (e) { /* thử module kế */ }
  }
  return null;
}

const pipes = {};        // handle(str) -> tên pipe
const pendingRead = {};  // lpOverlapped(str) -> {h, buf}

const GetFileType = new NativeFunction(exp('GetFileType'), 'uint32', ['pointer']);
const GetFileInfoEx = exp('GetFileInformationByHandleEx')
  ? new NativeFunction(exp('GetFileInformationByHandleEx'), 'int', ['pointer', 'int', 'pointer', 'uint32'])
  : null;
const FILE_TYPE_PIPE = 3;
const FileNameInfo = 2;

function log(msg) { send({ type: 'log', msg: msg }); }

function pipeName(h) {
  const k = h.toString();
  if (k in pipes) return pipes[k];
  if (!ALL_PIPES) return null;
  // Attach muộn: pipe đã mở trước khi hook -> nhận diện bằng GetFileType.
  let t;
  try { t = GetFileType(h); } catch (e) { return null; }
  if (t !== FILE_TYPE_PIPE) return null;
  let name = '<pipe?>';
  if (GetFileInfoEx) {
    const buf = Memory.alloc(1024);
    if (GetFileInfoEx(h, FileNameInfo, buf, 1024)) {
      const len = buf.readU32();
      name = '\\\\.\\pipe' + buf.add(4).readUtf16String(len / 2);
    }
  }
  pipes[k] = name;
  log('Phát hiện pipe đã mở sẵn: handle=' + k + ' ' + name);
  return name;
}

function emit(dir, h, buf, n) {
  if (n <= 0 || buf.isNull()) return;
  send({ type: 'packet', dir: dir, handle: h.toString(), pipe: pipes[h.toString()] || '?',
         len: n, tid: Process.getCurrentThreadId() }, buf.readByteArray(n));
}

function isPipePath(s) { return s && s.toLowerCase().indexOf('\\pipe\\') >= 0; }

function hook(name, cb) {
  const p = exp(name);
  if (!p) { log('Không tìm thấy ' + name); return; }
  Interceptor.attach(p, cb);
}

hook('CreateFileW', {
  onEnter(a) { this.path = a[0].isNull() ? null : a[0].readUtf16String(); },
  onLeave(r) {
    if (!isPipePath(this.path)) return;
    const ok = r.toInt32() !== -1;
    if (ok) pipes[r.toString()] = this.path;
    send({ type: 'open', api: 'CreateFileW', pipe: this.path, handle: r.toString(), ok: ok });
  }
});

hook('CreateNamedPipeW', {
  onEnter(a) { this.path = a[0].readUtf16String(); this.mode = a[1].toInt32(); this.pmode = a[2].toInt32(); },
  onLeave(r) {
    const ok = r.toInt32() !== -1;
    if (ok) pipes[r.toString()] = this.path;
    send({ type: 'open', api: 'CreateNamedPipeW', pipe: this.path, handle: r.toString(), ok: ok,
           openMode: this.mode, pipeMode: this.pmode });
  }
});

hook('WaitNamedPipeW', {
  onEnter(a) { send({ type: 'open', api: 'WaitNamedPipeW', pipe: a[0].readUtf16String(), handle: '', ok: true }); }
});

hook('ConnectNamedPipe', {
  onEnter(a) { log('ConnectNamedPipe handle=' + a[0] + ' (' + (pipes[a[0].toString()] || '?') + ')'); }
});

hook('WriteFile', {
  // BOOL WriteFile(h, lpBuffer, nBytes, lpWritten, lpOverlapped)
  onEnter(a) {
    if (pipeName(a[0]) === null) return;
    emit('W', a[0], a[1], a[2].toUInt32());   // dữ liệu có sẵn ngay khi gọi
  }
});

hook('ReadFile', {
  // BOOL ReadFile(h, lpBuffer, nBytes, lpRead, lpOverlapped)
  onEnter(a) {
    this.h = a[0];
    this.skip = pipeName(a[0]) === null;
    this.buf = a[1]; this.pRead = a[3]; this.ovl = a[4];
  },
  onLeave(r) {
    if (this.skip) return;
    if (r.toInt32() !== 0 && !this.pRead.isNull()) {
      emit('R', this.h, this.buf, this.pRead.readU32());       // đọc đồng bộ
    } else if (!this.ovl.isNull()) {
      pendingRead[this.ovl.toString()] = { h: this.h, buf: this.buf };  // overlapped: chờ hoàn tất
      if (r.toInt32() !== 0) {
        // Hoàn tất ngay: số byte nằm ở OVERLAPPED.InternalHigh (offset 4)
        emit('R', this.h, this.buf, this.ovl.add(4).readU32());
        delete pendingRead[this.ovl.toString()];
      }
    }
  }
});

hook('GetOverlappedResult', {
  // BOOL GetOverlappedResult(h, lpOverlapped, lpTransferred, bWait)
  onEnter(a) { this.ovl = a[1]; this.pN = a[2]; },
  onLeave(r) {
    const k = this.ovl.toString();
    if (r.toInt32() === 0 || !(k in pendingRead)) return;
    const p = pendingRead[k]; delete pendingRead[k];
    emit('R', p.h, p.buf, this.pN.readU32());
  }
});

hook('CloseHandle', {
  onEnter(a) {
    const k = a[0].toString();
    if (k in pipes) { send({ type: 'close', handle: k, pipe: pipes[k] }); delete pipes[k]; }
  }
});

log('Đã hook xong trong PID ' + Process.id + ' (' + Process.arch + ')');
