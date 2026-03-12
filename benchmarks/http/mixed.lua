math.randomseed(os.time())

local threads = {}

setup = function(thread)
  thread:set("status_2xx", 0)
  thread:set("status_4xx", 0)
  thread:set("status_5xx", 0)
  thread:set("status_other", 0)
  table.insert(threads, thread)
end

request = function()
  local bucket = math.random(100)
  local path

  if bucket <= 60 then
    path = "/ping"
  elseif bucket <= 80 then
    path = "/user/me"
  else
    path = "/user/" .. tostring(math.random(1, 10000))
  end

  return wrk.format("GET", path)
end

response = function(status, headers, body)
  if status >= 200 and status < 300 then
    status_2xx = status_2xx + 1
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
    ["2xx"] = 0,
    ["4xx"] = 0,
    ["5xx"] = 0,
    other = 0
  }

  for _, thread in ipairs(threads) do
    totals["2xx"] = totals["2xx"] + thread:get("status_2xx")
    totals["4xx"] = totals["4xx"] + thread:get("status_4xx")
    totals["5xx"] = totals["5xx"] + thread:get("status_5xx")
    totals.other = totals.other + thread:get("status_other")
  end

  io.write(string.format(
    "status_2xx=%d status_4xx=%d status_5xx=%d status_other=%d\n",
    totals["2xx"], totals["4xx"], totals["5xx"], totals.other
  ))
end
