local req = table.concat({
  "POST /echo HTTP/1.1\r\n",
  "Host: 127.0.0.1\r\n",
  "Content-Type: text/plain\r\n",
  "Content-Length: 11\r\n",
  "Connection: close\r\n",
  "\r\n",
  "hello world"
})

request = function()
  return req
end
