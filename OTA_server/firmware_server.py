import http.server
import ssl
import os
import re

PORT = 8070
CERT_FILE = "server.crt"
KEY_FILE = "server.key"

class RangeHTTPServer(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            return super().send_head()

        f = open(path, 'rb')
        size = os.fstat(f.fileno()).st_size
        range_header = self.headers.get('Range')

        if range_header:
            match = re.match(r'bytes=(\d+)-(\d*)', range_header)
            if match:
                start = int(match.group(1))
                end = int(match.group(2)) if match.group(2) else size - 1
                self.send_response(206)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
                self.send_header('Content-Length', str(end - start + 1))
                self.end_headers()
                f.seek(start)
                return f

        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(size))
        self.end_headers()
        return f

    def copyfile(self, source, outputfile):
        range_header = self.headers.get('Range')
        if range_header:
            match = re.match(r'bytes=(\d+)-(\d*)', range_header)
            if match:
                start = int(match.group(1))
                size = os.fstat(source.fileno()).st_size
                end = int(match.group(2)) if match.group(2) else size - 1
                remaining = end - start + 1
                try:
                    while remaining > 0:
                        read_size = min(remaining, 16384)
                        buf = source.read(read_size)
                        if not buf: break
                        outputfile.write(buf) # If ESP32 closes, this triggers the error
                        remaining -= len(buf)
                except (ConnectionResetError, BrokenPipeError):
                    print("ℹ️ Client closed connection (likely version match or abort).")
                return
        super().copyfile(source, outputfile)

if __name__ == "__main__":
    server_address = ('0.0.0.0', PORT)
    httpd = http.server.HTTPServer(server_address, RangeHTTPServer)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=CERT_FILE, keyfile=KEY_FILE)
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    print(f"OTA Server running on https://localhost:{PORT}")
    httpd.serve_forever()
