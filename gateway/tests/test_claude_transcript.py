import pytest

from stackchan_mcp.agent_adapters.claude_transcript import ClaudeTranscript


def event(kind, content, **extra):
    return {"type": kind, "sessionId": "claude-1", "message": {"content": content}, **extra}


def tool(name, fields, tool_id="use-1"):
    return event("assistant", [{"type": "tool_use", "name": name, "id": tool_id, "input": fields}])


def result(fields, *, failed=False):
    return event("user", [{"type": "tool_result", "tool_use_id": "use-1", "is_error": failed}], toolUseResult=fields)


def test_task_creation_only_applies_after_successful_result():
    adapter = ClaudeTranscript()
    snapshot = adapter.consume(tool("TaskCreate", {"subject": "编译看板", "description": "private"}))
    assert snapshot["tasks"] == []
    snapshot = adapter.consume(result({"task": {"id": "7", "subject": "编译看板", "status": "pending"}}))
    assert snapshot["tasks"] == [{"id": "task-7", "text": "编译看板", "status": "pending"}]
    assert "private" not in str(snapshot)


def test_task_update_and_completion_keep_task_titles():
    adapter = ClaudeTranscript()
    adapter.consume(tool("TaskCreate", {"subject": "验证连接"}))
    adapter.consume(result({"task": {"id": "7", "subject": "验证连接"}}))
    adapter.consume(tool("TaskUpdate", {"taskId": "7", "status": "in_progress"}))
    snapshot = adapter.consume(result({"success": True, "taskId": "7"}))
    assert snapshot["tasks"][0]["status"] == "in_progress"
    snapshot = adapter.consume(event("assistant", [], message={"content": [], "stop_reason": "end_turn"}))
    assert snapshot["status"] == "completed"
    assert snapshot["tasks"][0]["text"] == "验证连接"


def test_failed_tool_does_not_change_tasks_and_waiting_question_is_identified():
    adapter = ClaudeTranscript()
    adapter.consume(tool("TodoWrite", {"todos": [{"content": "不应显示", "status": "pending"}]}))
    snapshot = adapter.consume(result({}, failed=True))
    assert snapshot["tasks"] == []
    snapshot = adapter.consume(tool("AskUserQuestion", {"questions": ["private"]}))
    assert snapshot["status"] == "waiting"
    assert "private" not in str(snapshot)


def test_todowrite_survives_lifecycle_events_and_empty_list_clears_tasks():
    adapter = ClaudeTranscript()
    adapter.consume(tool("TodoWrite", {"todos": [{"content": "处理任务", "status": "in_progress"}]}))
    snapshot = adapter.consume(result({}))
    assert len(snapshot["tasks"]) == 1
    snapshot = adapter.consume(event("user", "private prompt"))
    assert len(snapshot["tasks"]) == 1
    assert snapshot["status"] == "working"
    adapter.consume(tool("TodoWrite", {"todos": []}))
    assert adapter.consume(result({}))["tasks"] == []


@pytest.mark.parametrize("extra", [{"isSidechain": True}, {"sessionId": "bad/session"}])
def test_sidechain_and_invalid_session_are_ignored(extra):
    adapter = ClaudeTranscript()
    assert adapter.consume(event("user", "private", **extra)) is None


def test_new_session_does_not_inherit_previous_tasks():
    adapter = ClaudeTranscript()
    adapter.consume(tool("TodoWrite", {"todos": [{"content": "旧会话", "status": "pending"}]}))
    adapter.consume(result({}))
    snapshot = adapter.consume(event("user", "private", sessionId="claude-2"))
    assert snapshot["tasks"] == []


def test_active_task_after_first_twelve_is_still_visible():
    adapter = ClaudeTranscript()
    for task_id in range(13):
        adapter.consume(tool("TaskCreate", {}))
        adapter.consume(result({"task": {"id": str(task_id), "subject": "任务"}}))
    adapter.consume(tool("TaskUpdate", {"taskId": "12", "status": "in_progress"}))
    snapshot = adapter.consume(result({"success": True, "taskId": "12"}))
    assert len(snapshot["tasks"]) == 12
    assert snapshot["tasks"][0]["id"] == "task-12"
