local threads = {}

setup = function(thread)
  thread:set("status_1xx", 0)
  thread:set("status_2xx", 0)
  thread:set("status_3xx", 0)
  thread:set("status_4xx", 0)
  thread:set("status_5xx", 0)
  thread:set("status_other", 0)
  table.insert(threads, thread)
end

response = function(status, headers, body)
  if status >= 100 and status < 200 then
    status_1xx = status_1xx + 1
  elseif status >= 200 and status < 300 then
    status_2xx = status_2xx + 1
  elseif status >= 300 and status < 400 then
    status_3xx = status_3xx + 1
  elseif status >= 400 and status < 500 then
    status_4xx = status_4xx + 1
  elseif status >= 500 and status < 600 then
    status_5xx = status_5xx + 1
  else
    status_other = status_other + 1
  end
end

done = function(summary, latency, requests)
  local totals = {
    ["1xx"] = 0,
    ["2xx"] = 0,
    ["3xx"] = 0,
    ["4xx"] = 0,
    ["5xx"] = 0,
    other = 0
  }

  for _, thread in ipairs(threads) do
    totals["1xx"] = totals["1xx"] + thread:get("status_1xx")
    totals["2xx"] = totals["2xx"] + thread:get("status_2xx")
    totals["3xx"] = totals["3xx"] + thread:get("status_3xx")
    totals["4xx"] = totals["4xx"] + thread:get("status_4xx")
    totals["5xx"] = totals["5xx"] + thread:get("status_5xx")
    totals.other = totals.other + thread:get("status_other")
  end

  io.write(string.format(
    "status_1xx=%d status_2xx=%d status_3xx=%d status_4xx=%d status_5xx=%d status_other=%d\n",
    totals["1xx"], totals["2xx"], totals["3xx"], totals["4xx"], totals["5xx"], totals.other
  ))
end
