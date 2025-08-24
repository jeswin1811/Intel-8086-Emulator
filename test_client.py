import socket, struct

HOST='127.0.0.1'
PORT=5555

with open('hello.com','rb') as f:
    data=f.read()

s=socket.create_connection((HOST,PORT))
s.sendall(struct.pack('<I', len(data)))
s.sendall(data)

out_len_bytes = s.recv(4)
if len(out_len_bytes) < 4:
    print('no response (len<4)')
    s.close(); raise SystemExit

out_len = struct.unpack('<I', out_len_bytes)[0]
print('out_len =', out_len)

out = b''
while len(out) < out_len:
    chunk = s.recv(out_len - len(out))
    if not chunk:
        break
    out += chunk

print('raw bytes:', out)
print('decoded:', out.decode('latin1', errors='replace'))
s.close()