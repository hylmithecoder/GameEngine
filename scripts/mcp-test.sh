#!/usr/bin/env bash
# Quick, read-only-by-default client for the local Ilmeee Editor MCP server.
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: MCP_TOKEN=... scripts/mcp-test.sh [info|objects|tools|watch|call NAME [JSON_ARGS]]

Commands:
  info             Show the active project and scene (default)
  objects          List scene objects once
  tools            List available MCP tools
  watch            Refresh the scene object list every second (Ctrl+C to stop)
  call NAME [JSON]  Call a tool; JSON arguments default to {}

Set MCP_PORT to override port 9220. If MCP_TOKEN is unset, you will be
prompted without echoing the token. 'call' can modify the scene.
USAGE
}

command -v curl >/dev/null || { echo 'curl is required' >&2; exit 1; }
command -v jq >/dev/null || { echo 'jq is required' >&2; exit 1; }

command_name=${1:-info}
if [[ $command_name == -h || $command_name == --help ]]; then
  usage
  exit 0
fi

port=${MCP_PORT:-9220}
if [[ ! $port =~ ^[0-9]+$ ]] || (( 10#$port < 1 || 10#$port > 65535 )); then
  echo 'MCP_PORT must be between 1 and 65535' >&2
  exit 1
fi
if [[ -z ${MCP_TOKEN:-} ]]; then
  read -rsp 'MCP token: ' MCP_TOKEN
  printf '\n' >&2
fi
if [[ -z $MCP_TOKEN ]]; then
  echo 'MCP token cannot be empty' >&2
  exit 1
fi

endpoint="http://127.0.0.1:${port}/mcp"

request() {
  local method=$1 params=$2 body reply
  body=$(jq -nc --arg method "$method" --argjson params "$params" \
    '{jsonrpc:"2.0",id:1,method:$method,params:$params}')
  reply=$(curl --silent --show-error --fail-with-body --max-time 10 \
    -H 'Accept: application/json, text/event-stream' \
    -H 'Content-Type: application/json' \
    -H 'MCP-Protocol-Version: 2025-11-25' \
    -H "Authorization: Bearer ${MCP_TOKEN}" \
    --data-binary "$body" "$endpoint") || {
      echo "MCP request failed at ${endpoint} (check editor, port, and token)." >&2
      return 1
    }
  if jq -e '.error != null or .result.isError == true' >/dev/null <<<"$reply"; then
    jq '.error // .result' <<<"$reply" >&2
    return 1
  fi
  jq '.result.structuredContent // .result' <<<"$reply"
}

call_tool() {
  local name=$1 args=${2:-{}}
  if ! jq -e 'type == "object"' >/dev/null <<<"$args"; then
    echo 'Tool arguments must be a JSON object.' >&2
    return 1
  fi
  request tools/call "$(jq -nc --arg name "$name" --argjson args "$args" \
    '{name:$name,arguments:$args}')"
}

case $command_name in
  info)    call_tool get_project_info ;;
  objects) call_tool list_scene_objects ;;
  tools)   request tools/list '{}' ;;
  watch)
    while :; do
      printf '\n%s  %s\n' "$(date '+%H:%M:%S')" "$endpoint"
      call_tool list_scene_objects
      sleep 1
    done
    ;;
  call)
    if [[ $# -lt 2 || $# -gt 3 ]]; then usage >&2; exit 1; fi
    call_tool "$2" "${3:-{}}"
    ;;
  *) usage >&2; exit 1 ;;
esac
