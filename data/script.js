// Canvas JS ADAPTED FROM https://codepen.io/dcode-software/pen/yLvWNpx
// If connecting without http (ex via local IP), port 81 is used for websocket connection.
// Otherwise, the program connects via port 443 which requires a reverse proxy server that will forwards /ws requests to port 81.
const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
const port = window.location.protocol === 'https:' ? '' : ':81';

let gateway = `${protocol}://${window.location.hostname}${port}/ws`;
let websocket;

function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.binaryType = "arraybuffer";
  websocket.onclose = onClose;
  websocket.onmessage = onMessage;
}

const canvas = document.getElementById("canvas");
const guide = document.getElementById("guide");
const clearButton = document.getElementById("clearButton");
const brushSizeSelector = document.getElementById('brush-select');
const drawing = canvas.getContext("2d");
const eraserToggle = document.getElementById("eraserToggleCheckbox");

canvas.width = 896;
canvas.height = 448;
let physicalDisplayWidth = 128;
let physicalDisplayHeight = 64;
const canvasMultiplier = 7;

let brushSize = 1;
let horizontalCellCount = physicalDisplayWidth / brushSize;
let verticalCellCount = physicalDisplayHeight / brushSize;
let cellSideLength = canvas.width / horizontalCellCount;
let lastX = -1;
let lastY = -1;

let eraserOn = false;
let eraserStateChanged = false;
let isDrawing = false;

drawing.fillStyle = "#242526";
drawing.fillRect(0, 0, canvas.width, canvas.height);
setupGridGuides();
initWebSocket();

// Reconnect to websocket in case of closed/lost connection.
function onClose(e) {
  console.log('Connection closed');
  setTimeout(initWebSocket, 2000);
}

// Apply pixel changes from the server to the canvas.
function parsePixelCommand(e) {
  const msg = JSON.parse(e.data);
  if (msg.clear) {
    drawing.fillStyle = "#242526";
    drawing.fillRect(0, 0, canvas.width, canvas.height);
  }
  else {
    if (msg.pixelOn) drawing.fillStyle = "#FFFFFF"
    else drawing.fillStyle = "#242526"
    const x = msg.x * canvasMultiplier;
    const y = msg.y * canvasMultiplier;
    const cellSideLength = canvas.width / (physicalDisplayWidth / msg.size);
    drawing.fillRect(x, y, cellSideLength, cellSideLength);
  }
}

// Extract each bit from the 1024 byte arrayBuffer received from the server and apply it to the canvas.
function parseCanvasState(e) {
  const pixels = new Uint8Array(e.data);
  drawing.fillStyle = "#FFFFFF";
  for (let y = 0; y < physicalDisplayHeight; y++) {
    for (let x = 0; x < physicalDisplayWidth; x++) {
      let byteIndex = Math.floor((y * physicalDisplayWidth + x) / 8);
      let bitIndex = 7 - (y * physicalDisplayWidth + x) % 8;
      let bit = (pixels[byteIndex] >> bitIndex) & 1;
      if (bit) drawing.fillRect(x * canvasMultiplier, y * canvasMultiplier, 7, 7);
    }
  }
}

// Receive websocket messages from the server and handle them accordingly.
function onMessage(e) {
  // If the data is a string, it is a command containing pixel data that was relayed by the server from a client.
  if (typeof e.data === "string") parsePixelCommand(e);
  // If the data is an arrayBuffer, it is a 1024 byte binary representation of the current state of the canvas on the server.
  else if (e.data instanceof ArrayBuffer) parseCanvasState(e);
}

// Sets up grid guides for the canvas based on the currently selected brush size.
function setupGridGuides() {
  const guideLines = guide.querySelectorAll('div');
  guideLines.forEach(line => line.remove());

  guide.style.width = `${canvas.width}px`;
  guide.style.height = `${canvas.height}px`;
  guide.style.gridTemplateColumns = `repeat(${horizontalCellCount}, 1fr)`;
  guide.style.gridTemplateRows = `repeat(${verticalCellCount}, 1fr)`;

  for (let i = 0; i < horizontalCellCount * verticalCellCount; i++) {
    guide.insertAdjacentHTML("beforeend", "<div></div>")
  }
}


// Sends a websocket message to the server indicating the pixel change.
function sendChangeToServer(cellx, celly) {
  const msg = {
    clear: false,
    pixelOn: true,
    x: Math.floor(cellx / canvasMultiplier),
    y: Math.floor(celly / canvasMultiplier),
    size: brushSize
  };
  if (eraserOn) msg.pixelOn = false;
  try {
    websocket.send(JSON.stringify(msg));
  }
  catch (error) {
    console.log(error);
  }
}

// Sets an x,y pixel using the currently selected brush size and eraser settings.
function fillCell(cellx, celly) {
  sendChangeToServer(cellx, celly);
  if (eraserOn) drawing.fillStyle = "#242526";
  else drawing.fillStyle = "#FFFFFF";
  drawing.fillRect(cellx, celly, cellSideLength, cellSideLength);
}

function mouseMoved(e) {
  const canvasBoundingRect = canvas.getBoundingClientRect();
  const x = e.clientX - canvasBoundingRect.left;
  const y = e.clientY - canvasBoundingRect.top;
  inputMoved(x,y);
}

function mouseDown(e) {
  isDrawing = true; 
  mouseMoved(e);
}

function mouseUp(e) {
  isDrawing = false; 
}

function touchMoved(e) {
  const canvasBoundingRect = canvas.getBoundingClientRect();
  const x = e.touches[0].clientX - canvasBoundingRect.left;
  const y = e.touches[0].clientY - canvasBoundingRect.top;
  inputMoved(x,y);
}

function touchStart(e) {
  // Prevent scrolling during canvas touch events.
  e.preventDefault();
  isDrawing = true; 
  touchMoved(e);
}

// Called by touchMoved and mouseMoved event handlers.
function inputMoved(x, y) {
  if (isDrawing) {
    const cellX = Math.floor(x / cellSideLength) * cellSideLength;
    const cellY = Math.floor(y / cellSideLength) * cellSideLength;
    if (cellX != lastX || cellY != lastY || eraserStateChanged) {
      eraserStateChanged = false;
      fillCell(cellX, cellY);
      lastX = cellX;
      lastY = cellY;
    }
  }
}

function clearCanvas() {
  const yes = confirm("Are you sure you wish to clear the canvas?");
  if (!yes) return;
  drawing.fillStyle = "#242526";
  drawing.fillRect(0, 0, canvas.width, canvas.height);
  const msg = {
    clear: true,
  };
  try {
    websocket.send(JSON.stringify(msg));
  }
  catch (error) {
    console.log(error);
  }
}

// Handle brush size changes
// Recalculate and apply updated scale values and pixel grid guidelines.
function brushChanged(e) {
  brushSize = parseInt(e.target.value);
  horizontalCellCount = 128 / brushSize;
  verticalCellCount = 64 / brushSize;
  cellSideLength = canvas.width / horizontalCellCount;
  setupGridGuides();
}

function eraserToggled(e) {
  if (eraserOn) eraserOn = false;
  else eraserOn = true;
  eraserStateChanged = true;
}

canvas.addEventListener("touchstart", touchStart, {passive: false});
canvas.addEventListener("touchend", mouseUp, {passive: false});
canvas.addEventListener("touchcancel", mouseUp, {passive: false});
canvas.addEventListener("touchmove", touchMoved, {passive: false});

canvas.addEventListener("mousemove", mouseMoved);
canvas.addEventListener("mousedown", mouseDown);
canvas.addEventListener("mouseup", mouseUp);
canvas.addEventListener("mouseout", mouseUp);

brushSizeSelector.addEventListener('change', brushChanged);
eraserToggle.addEventListener('change', eraserToggled);
clearButton.addEventListener("click", clearCanvas);