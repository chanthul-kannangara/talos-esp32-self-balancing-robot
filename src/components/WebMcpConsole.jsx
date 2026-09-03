import React, { useState } from "react";
import { Bot, CheckCircle2, ChevronRight, Code2, Play, Terminal, X, Zap, Shield, Cpu } from "lucide-react";

export default function WebMcpConsole({ isOpen, onClose, tools, executionLogs, executeTool }) {
  const [activeTab, setActiveTab] = useState("tools");
  const [selectedTool, setSelectedTool] = useState(tools[0]?.name || "talos_get_telemetry");
  const [toolInputs, setToolInputs] = useState({});
  const [testResult, setTestResult] = useState(null);
  const [isExecuting, setIsExecuting] = useState(false);

  if (!isOpen) return null;

  const currentToolObj = tools.find((t) => t.name === selectedTool) || tools[0];

  const handleInputChange = (paramName, value) => {
    setToolInputs((prev) => ({ ...prev, [paramName]: value }));
  };

  const handleRunTool = async () => {
    if (!currentToolObj) return;
    setIsExecuting(true);
    setTestResult(null);
    try {
      // parse parameters based on schema
      const parsedArgs = {};
      const properties = currentToolObj.inputSchema?.properties || {};

      for (const [key, schema] of Object.entries(properties)) {
        let val = toolInputs[key];
        if (val === undefined || val === "") {
          if (schema.enum) val = schema.enum[0];
          else if (schema.type === "number" || schema.type === "integer") val = schema.minimum || 50;
          else if (schema.type === "boolean") val = true;
        }

        if (schema.type === "number" || schema.type === "integer") {
          parsedArgs[key] = Number(val);
        } else if (schema.type === "boolean") {
          parsedArgs[key] = val === true || val === "true";
        } else {
          parsedArgs[key] = val;
        }
      }

      const res = await executeTool(currentToolObj.name, parsedArgs);
      setTestResult({ success: true, data: res });
    } catch (err) {
      setTestResult({ success: false, error: err.message || String(err) });
    } finally {
      setIsExecuting(false);
    }
  };

  return (
    <div className="webmcp-overlay" role="dialog" aria-modal="true" aria-labelledby="webmcp-title">
      <div className="webmcp-backdrop" onClick={onClose} />
      <div className="webmcp-modal panel">
        {/* Header */}
        <div className="webmcp-header">
          <div className="webmcp-title-group">
            <div className="webmcp-badge">
              <Bot size={18} className="webmcp-icon-glow" />
              <span>WebMCP Standard 1.0</span>
            </div>
            <h2 id="webmcp-title" className="webmcp-title">
              Agent Model Context Protocol Interface
            </h2>
            <p className="webmcp-subtitle">
              Exposed via <code>navigator.modelContext</code> and <code>window.__WEBMCP__</code> for autonomous AI agents.
            </p>
          </div>
          <button className="webmcp-close-btn" onClick={onClose} aria-label="Close WebMCP Console">
            <X size={20} />
          </button>
        </div>

        {/* Status Bar */}
        <div className="webmcp-status-bar">
          <div className="webmcp-status-chip is-active">
            <Zap size={14} />
            <span>W3C WebML Compliant</span>
          </div>
          <div className="webmcp-status-chip">
            <Cpu size={14} />
            <span>{tools.length} Tools Registered</span>
          </div>
          <div className="webmcp-status-chip">
            <Shield size={14} />
            <span>Safe Sandbox Execution</span>
          </div>
        </div>

        {/* Tabs */}
        <div className="webmcp-tabs">
          <button
            className={`webmcp-tab-btn ${activeTab === "tools" ? "is-active" : ""}`}
            onClick={() => setActiveTab("tools")}
          >
            <Code2 size={16} />
            Tools Catalog ({tools.length})
          </button>
          <button
            className={`webmcp-tab-btn ${activeTab === "logs" ? "is-active" : ""}`}
            onClick={() => setActiveTab("logs")}
          >
            <Terminal size={16} />
            Agent Activity Log ({executionLogs.length})
          </button>
          <button
            className={`webmcp-tab-btn ${activeTab === "docs" ? "is-active" : ""}`}
            onClick={() => setActiveTab("docs")}
          >
            <Bot size={16} />
            Agent Prompt Guide
          </button>
        </div>

        {/* Content Area */}
        <div className="webmcp-content">
          {activeTab === "tools" && (
            <div className="webmcp-grid">
              {/* Tool List Sidebar */}
              <div className="webmcp-tool-list">
                {tools.map((t) => (
                  <button
                    key={t.name}
                    className={`webmcp-tool-item ${selectedTool === t.name ? "is-selected" : ""}`}
                    onClick={() => {
                      setSelectedTool(t.name);
                      setTestResult(null);
                    }}
                  >
                    <div className="webmcp-tool-item-info">
                      <span className="webmcp-tool-item-name">{t.name}</span>
                      <span className="webmcp-tool-item-desc">{t.description}</span>
                    </div>
                    <ChevronRight size={14} className="webmcp-tool-item-arrow" />
                  </button>
                ))}
              </div>

              {/* Tool Detail & Interactive Tester */}
              {currentToolObj && (
                <div className="webmcp-tester">
                  <div className="webmcp-tester-card">
                    <div className="webmcp-tester-header">
                      <div>
                        <span className="section-kicker">W3C WebMCP Tool</span>
                        <h3>{currentToolObj.name}</h3>
                      </div>
                      <button
                        className="btn btn--primary webmcp-run-btn"
                        onClick={handleRunTool}
                        disabled={isExecuting}
                      >
                        <Play size={14} />
                        {isExecuting ? "Executing..." : "Test Execute Tool"}
                      </button>
                    </div>

                    <p className="webmcp-tester-desc">{currentToolObj.description}</p>

                    {/* Parameter Form */}
                    {currentToolObj.inputSchema?.properties &&
                      Object.keys(currentToolObj.inputSchema.properties).length > 0 && (
                        <div className="webmcp-params-form">
                          <h4>Input Parameters</h4>
                          {Object.entries(currentToolObj.inputSchema.properties).map(([paramName, schema]) => (
                            <div key={paramName} className="webmcp-form-row">
                              <label>
                                <code>{paramName}</code>
                                <span className="param-type">({schema.type})</span>
                              </label>
                              {schema.enum ? (
                                <select
                                  className="input-select"
                                  value={toolInputs[paramName] || schema.enum[0]}
                                  onChange={(e) => handleInputChange(paramName, e.target.value)}
                                >
                                  {schema.enum.map((opt) => (
                                    <option key={opt} value={opt}>
                                      {opt}
                                    </option>
                                  ))}
                                </select>
                              ) : schema.type === "boolean" ? (
                                <select
                                  className="input-select"
                                  value={toolInputs[paramName] ?? "true"}
                                  onChange={(e) => handleInputChange(paramName, e.target.value === "true")}
                                >
                                  <option value="true">true</option>
                                  <option value="false">false</option>
                                </select>
                              ) : (
                                <input
                                  type={schema.type === "number" || schema.type === "integer" ? "number" : "text"}
                                  className="input-field"
                                  placeholder={schema.description}
                                  value={toolInputs[paramName] ?? (schema.minimum ?? "")}
                                  onChange={(e) => handleInputChange(paramName, e.target.value)}
                                />
                              )}
                              <span className="param-desc">{schema.description}</span>
                            </div>
                          ))}
                        </div>
                      )}

                    {/* Execution Result Box */}
                    {testResult && (
                      <div className={`webmcp-result-box ${testResult.success ? "is-success" : "is-error"}`}>
                        <div className="webmcp-result-header">
                          <CheckCircle2 size={14} />
                          <span>Tool Execution Output</span>
                        </div>
                        <pre>{JSON.stringify(testResult.data || testResult.error, null, 2)}</pre>
                      </div>
                    )}

                    {/* JSON Schema Preview */}
                    <div className="webmcp-schema-box">
                      <h4>Model Context Protocol Schema</h4>
                      <pre>{JSON.stringify(currentToolObj.inputSchema, null, 2)}</pre>
                    </div>
                  </div>
                </div>
              )}
            </div>
          )}

          {activeTab === "logs" && (
            <div className="webmcp-logs">
              {executionLogs.length === 0 ? (
                <div className="webmcp-empty-logs">
                  <Terminal size={32} />
                  <p>No tool executions yet. Test a tool from the catalog to see real-time execution logs!</p>
                </div>
              ) : (
                <div className="webmcp-logs-list">
                  {executionLogs.map((log) => (
                    <div key={log.id} className={`webmcp-log-card ${log.success ? "is-ok" : "is-failed"}`}>
                      <div className="webmcp-log-meta">
                        <span className="webmcp-log-time">{new Date(log.timestamp).toLocaleTimeString()}</span>
                        <span className="webmcp-log-tool">{log.name}</span>
                        <span className="webmcp-log-duration">{log.durationMs}ms</span>
                        <span className={`webmcp-log-status ${log.success ? "status--ok" : "status--err"}`}>
                          {log.success ? "SUCCESS" : "ERROR"}
                        </span>
                      </div>
                      <div className="webmcp-log-body">
                        <div className="webmcp-log-section">
                          <strong>Arguments:</strong>
                          <pre>{JSON.stringify(log.args, null, 2)}</pre>
                        </div>
                        <div className="webmcp-log-section">
                          <strong>Result:</strong>
                          <pre>{JSON.stringify(log.result || log.error, null, 2)}</pre>
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          {activeTab === "docs" && (
            <div className="webmcp-docs">
              <div className="webmcp-docs-card">
                <h3>Connecting Autonomous AI Agents to TALOS via WebMCP</h3>
                <p>
                  Any browser-based AI agent (including Chrome Model Context Extensions, OpenAI Operator, or custom autonomous agents)
                  can discover the tools on this page via:
                </p>
                <pre><code>{`// Native W3C WebML Standard
const tools = navigator.modelContext.getTools();

// Execute a tool directly
const telemetry = await navigator.modelContext.executeTool("talos_get_telemetry");

// Drive the robot forward
await navigator.modelContext.executeTool("talos_motion_command", { direction: "forward" });

// Dynamically lower ride height
await navigator.modelContext.executeTool("talos_set_leg_posture", { legPosition: 30, legSpeed: 80 });`}</code></pre>

                <h4>Example Agent System Prompt</h4>
                <pre><code>{`You are an autonomous robotics agent connected to the TALOS two-wheeled self-balancing robot through the WebMCP (Web Model Context Protocol) interface.
Available Tools:
- talos_get_telemetry: monitor robot pitch, battery, and stabilization.
- talos_motion_command: command directional movement (forward, backward, left, right, stop).
- talos_set_leg_posture: actively adjust ride height and leg extension.
- talos_emergency_stop: instantly disarm motors in emergency.

Always check telemetry before commanding high-speed maneuvers, and monitor tiltAlert.`}</code></pre>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
