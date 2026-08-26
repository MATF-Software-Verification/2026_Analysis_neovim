-- Headless profiling workload for neovim v0.12.5.
-- Exercises: buffer line storage, the extmark tree (src/nvim/marktree.c),
-- the regex substitution engine, and the fuzzy-matching algorithm
-- (src/nvim/fuzzy.c, via vim.fn.matchfuzzy).
--
-- Scale down via NVIM_PROFILE_SCALE (0.0-1.0) for slow instrumented runs
-- (e.g. valgrind --tool=callgrind).

local scale = tonumber(os.getenv("NVIM_PROFILE_SCALE") or "1.0")

local NLINES = math.floor(200000 * scale)
local NEXTMARKS = math.floor(50000 * scale)
local NCANDIDATES = math.floor(20000 * scale)
local NFUZZY_QUERIES = math.max(1, math.floor(20 * scale))

local t0 = os.clock()

-- 1. Populate a large buffer.
local lines = {}
for i = 1, NLINES do
  lines[i] = string.format("line_%08d the quick brown fox jumps over the lazy dog %d", i, i)
end
vim.api.nvim_buf_set_lines(0, 0, -1, false, lines)
local t1 = os.clock()

-- 2. Stress marktree.c: create many extmarks scattered across the buffer.
local ns = vim.api.nvim_create_namespace("profiling")
for i = 1, NEXTMARKS do
  local row = (i * 7) % math.max(1, NLINES)
  vim.api.nvim_buf_set_extmark(0, ns, row, 0, {})
end
local t2 = os.clock()

-- 3. Stress the regex/substitution engine.
vim.cmd([[silent! %s/quick/QUICK/g]])
vim.cmd([[silent! %s/QUICK/quick/g]])
local t3 = os.clock()

-- 4. Stress fuzzy.c via matchfuzzy().
local candidates = {}
for i = 1, NCANDIDATES do
  candidates[i] = string.format("candidate_item_%06d_%s", i, ("abcdefghij"):sub(1, (i % 10) + 1))
end
for _ = 1, NFUZZY_QUERIES do
  vim.fn.matchfuzzy(candidates, "cand123")
end
local t4 = os.clock()

io.stderr:write(string.format(
  "workload: scale=%.3f nlines=%d nextmarks=%d ncandidates=%d nqueries=%d\n" ..
  "  populate_buffer=%.3fs  extmarks=%.3fs  substitute=%.3fs  fuzzy=%.3fs  total=%.3fs\n",
  scale, NLINES, NEXTMARKS, NCANDIDATES, NFUZZY_QUERIES,
  t1 - t0, t2 - t1, t3 - t2, t4 - t3, t4 - t0))

vim.cmd("qa!")
