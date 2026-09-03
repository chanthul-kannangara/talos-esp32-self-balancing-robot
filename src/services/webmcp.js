/**
 * WebMCP (Web Model Context Protocol) Standard Bridge & Polyfill
 * Conforms to the W3C Web Machine Learning Community Group specification.
 * 
 * Provides navigator.modelContext and document.modelContext interfaces
 * allowing browser AI agents to discover and execute structured tools.
 */

class WebModelContextRegistry {
  constructor() {
    this.tools = new Map();
    this.listeners = new Set();
    this.executionHistory = [];
  }

  /**
   * Register a new tool with the WebMCP environment
   * @param {Object} tool
   * @param {string} tool.name - Unique identifier for the tool
   * @param {string} tool.description - Clear explanation of tool purpose and usage for LLM
   * @param {Object} tool.inputSchema - JSON Schema definition of parameters
   * @param {Function} tool.execute - Async callback executing the tool logic
   */
  registerTool(tool) {
    if (!tool || typeof tool !== "object") {
      throw new TypeError("WebMCP tool definition must be an object.");
    }
    if (!tool.name || typeof tool.name !== "string") {
      throw new TypeError("WebMCP tool must have a valid string 'name'.");
    }
    if (!tool.description || typeof tool.description !== "string") {
      throw new TypeError("WebMCP tool must have a valid string 'description'.");
    }
    if (typeof tool.execute !== "function") {
      throw new TypeError(`WebMCP tool '${tool.name}' must have an 'execute' function.`);
    }

    const toolEntry = {
      name: tool.name,
      description: tool.description,
      inputSchema: tool.inputSchema || { type: "object", properties: {} },
      execute: tool.execute,
      registeredAt: Date.now(),
    };

    this.tools.set(tool.name, toolEntry);
    this._notifyListeners("registered", toolEntry);

    // Dispatch standard W3C WebMCP events
    if (typeof window !== "undefined") {
      const eventDetail = { tool: toolEntry, tools: this.getTools() };
      window.dispatchEvent(new CustomEvent("modelcontexttooladded", { detail: eventDetail }));
      window.dispatchEvent(new CustomEvent("modelcontextchanged", { detail: eventDetail }));
      if (typeof document !== "undefined") {
        document.dispatchEvent(new CustomEvent("modelcontexttooladded", { detail: eventDetail }));
      }
    }

    return toolEntry;
  }

  /**
   * Unregister an existing tool
   * @param {string} name
   */
  unregisterTool(name) {
    const existed = this.tools.delete(name);
    if (existed) {
      this._notifyListeners("unregistered", { name });
      if (typeof window !== "undefined") {
        const eventDetail = { name, tools: this.getTools() };
        window.dispatchEvent(new CustomEvent("modelcontexttoolremoved", { detail: eventDetail }));
        window.dispatchEvent(new CustomEvent("modelcontextchanged", { detail: eventDetail }));
      }
    }
    return existed;
  }

  /**
   * Return all currently registered tools formatted for agent consumption
   */
  getTools() {
    return Array.from(this.tools.values()).map(({ name, description, inputSchema, execute }) => ({
      name,
      description,
      inputSchema,
      execute,
    }));
  }

  /**
   * Alias for getTools() to support evolving W3C spec variants
   */
  listTools() {
    return this.getTools();
  }

  /**
   * Execute a tool by name with arguments
   * @param {string} name
   * @param {Object} args
   */
  async executeTool(name, args = {}) {
    const tool = this.tools.get(name);
    if (!tool) {
      throw new Error(`WebMCP: Tool '${name}' is not registered.`);
    }

    const startTime = Date.now();
    try {
      const result = await tool.execute(args);
      const executionRecord = {
        id: `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
        name,
        args,
        result,
        success: true,
        durationMs: Date.now() - startTime,
        timestamp: new Date().toISOString(),
      };
      this.executionHistory.unshift(executionRecord);
      if (this.executionHistory.length > 50) this.executionHistory.pop();
      this._notifyListeners("executed", executionRecord);

      if (typeof window !== "undefined") {
        window.dispatchEvent(
          new CustomEvent("modelcontextexecute", { detail: executionRecord })
        );
      }

      return result;
    } catch (error) {
      const errorRecord = {
        id: `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
        name,
        args,
        error: error.message || String(error),
        success: false,
        durationMs: Date.now() - startTime,
        timestamp: new Date().toISOString(),
      };
      this.executionHistory.unshift(errorRecord);
      this._notifyListeners("executed", errorRecord);
      throw error;
    }
  }

  /**
   * Subscribe to registry changes (tools added, removed, executed)
   */
  subscribe(callback) {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }

  _notifyListeners(event, data) {
    for (const callback of this.listeners) {
      try {
        callback(event, data, this.getTools());
      } catch (err) {
        console.error("WebMCP listener error:", err);
      }
    }
  }
}

// Global Singleton Registry
export const webModelContext = new WebModelContextRegistry();

/**
 * Initialize WebMCP global interfaces on window, navigator, and document
 */
export function initWebMCP() {
  if (typeof window === "undefined") return;

  // Polyfill / expose on navigator.modelContext
  if (!navigator.modelContext) {
    try {
      Object.defineProperty(navigator, "modelContext", {
        value: webModelContext,
        writable: true,
        configurable: true,
      });
    } catch {
      navigator.modelContext = webModelContext;
    }
  }

  // Polyfill / expose on document.modelContext
  if (typeof document !== "undefined" && !document.modelContext) {
    try {
      Object.defineProperty(document, "modelContext", {
        value: webModelContext,
        writable: true,
        configurable: true,
      });
    } catch {
      document.modelContext = webModelContext;
    }
  }

  // Also expose on window for direct access & ChromeLabs inspector compatibility
  window.modelContext = webModelContext;
  window.__WEBMCP__ = {
    version: "1.0.0",
    standard: "W3C Web Machine Learning CG - WebMCP",
    registry: webModelContext,
    getTools: () => webModelContext.getTools(),
    execute: (name, args) => webModelContext.executeTool(name, args),
  };

  console.log(
    "%c[WebMCP 1.0]%c Web Model Context Protocol initialized on navigator.modelContext & window.__WEBMCP__",
    "background: #2563eb; color: #fff; padding: 2px 6px; border-radius: 4px; font-weight: bold;",
    "color: #3b82f6; font-weight: bold;"
  );
}
