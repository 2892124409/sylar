local req = table.concat({
  "POST /echo HTTP/1.1\r\n",
  "Host: 127.0.0.1\r\n",
  "Content-Type: text/plain\r\n",
  "Transfer-Encoding: chunked\r\n",
  "Connection: keep-alive\r\n",
  "\r\n",
  "5\r\nhello\r\n",
  "6\r\n world\r\n",
  "0\r\n",
  "\r\n"
})

request = function()
  return req
end
