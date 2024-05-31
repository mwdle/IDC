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

// Distribution of threshold values for color to monochrome image conversions.
let threshold = [ 0.25, 0.26, 0.27, 0.28, 0.29, 0.3, 0.31, 0.32, 
  0.33, 0.34, 0.35, 0.36, 0.37, 0.38, 0.39, 0.4, 0.41, 0.42,
  0.43, 0.44, 0.45, 0.46, 0.47, 0.48, 0.49, 0.5, 0.51, 0.52, 0.53,
  0.54, 0.55, 0.56, 0.57, 0.58, 0.59, 0.6, 0.61, 0.62, 0.63, 0.64,
  0.65, 0.66, 0.67, 0.68, 0.69 ];

// Reconnect to websocket in case of closed/lost connection.
function onClose(e) {
  console.log('Connection closed');
  setTimeout(initWebSocket, 2000);
}

function sendMessageToServer(data) {
  try {
    websocket.send(data);
  }
  catch (error) {
    console.log(error);
  } 
}

// Converts color image data into monochrome by randomly selecting threshold values from a distribution to produce a dithering effect.
// Expects an image size of 8192 pixels.
function dither(imgCtx, binaryRepresentation) {
  let imageData = imgCtx.getImageData(0, 0, physicalDisplayWidth, physicalDisplayHeight).data;
  for (let i = 0; i < physicalDisplayHeight*physicalDisplayWidth*4; i += 4) {
    const lum = ((imageData[i] + imageData[i + 1] + imageData[i + 2]) / 3) / 255;
    let pixelNum = i / 4;
    let byteIndex = Math.floor(pixelNum / 8);
    let bitIndex = 7 - ((pixelNum) % 8);
    let color = (lum >= threshold[Math.floor(Math.random() * threshold.length)]) ? 1 : 0;
    binaryRepresentation[byteIndex] |= color << bitIndex;
  }
} 

// Scales an image to fit physical display, converts it to black and white, and sends it to the server.
function uploadImageToServer(e) {
    let image = new Image();
    image.src = window.URL.createObjectURL(document.getElementById('imageUpload').files[0]);
    image.onload = function() {
      let upload = document.createElement('canvas');
      let ctx = upload.getContext('2d');

      // Scale the image to fit the physical display.
      // let scaleFactor = Math.min(physicalDisplayWidth / image.width, physicalDisplayHeight / image.height);
      upload.width = image.width * physicalDisplayWidth / image.width;
      upload.height = image.height * physicalDisplayHeight / image.height;
      ctx.drawImage(image, 0, 0, physicalDisplayWidth, physicalDisplayHeight);

      // Convert the image to black and white and store it in a binary format to send to the server.
      let binaryRepresentation = new Uint8Array(1024);
      dither(ctx, binaryRepresentation);
      sendMessageToServer(binaryRepresentation.buffer);
  };
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
  for (let y = 0; y < physicalDisplayHeight; y++) {
    for (let x = 0; x < physicalDisplayWidth; x++) {
      let byteIndex = Math.floor((y * physicalDisplayWidth + x) / 8);
      let bitIndex = 7 - (y * physicalDisplayWidth + x) % 8;
      let bit = (pixels[byteIndex] >> bitIndex) & 1;
      if (bit) drawing.fillStyle = "#FFFFFF";
      else drawing.fillStyle = "#242526";
      drawing.fillRect(x * canvasMultiplier, y * canvasMultiplier, 7, 7);
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

function requestImageSaveOnServer() {
  const msg = {
    clear: false,
    saveCanvasToFile: true
  };
  sendMessageToServer(JSON.stringify(msg));
}

function requestNextSavedImageFromServer() {
  const msg = {
    clear: false,
    saveCanvasToFile: false,
    showNextSavedImage: true
  };
  sendMessageToServer(JSON.stringify(msg));
}

// Sends a websocket message to the server indicating the pixel change.
function sendPixelChangeToServer(cellx, celly) {
  const msg = {
    clear: false,
    saveCanvasToFile: false,
    showNextSavedImage: false,
    pixelOn: !eraserOn,
    x: Math.floor(cellx / canvasMultiplier),
    y: Math.floor(celly / canvasMultiplier),
    size: brushSize
  };
  sendMessageToServer(JSON.stringify(msg));
}

// Sets an x,y pixel using the currently selected brush size and eraser settings.
function fillCell(cellx, celly) {
  sendPixelChangeToServer(cellx, celly);
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
  sendMessageToServer(JSON.stringify(msg));
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

function downloadCanvas() {
  let dataURL = canvas.toDataURL("image/png");
  let a = document.createElement('a');
  a.href = dataURL
  a.download = "icc";
  a.click();
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

document.getElementById('downloadButton').addEventListener('click', downloadCanvas)
document.getElementById('imageUpload').addEventListener('change', uploadImageToServer);
document.getElementById('saveOnServerButton').addEventListener('click', requestImageSaveOnServer);
document.getElementById('requestNextImageButton').addEventListener('click', requestNextSavedImageFromServer);

document.getElementById('imageUploadButton').addEventListener('click', function() {
  document.getElementById('imageUpload').click();
})

canvas.addEventListener("dragover", function(e) {
  e.preventDefault();
})

canvas.addEventListener("drop", function(e) {
  e.preventDefault();
  document.getElementById('imageUpload').files = e.dataTransfer.files;
  uploadImageToServer(e);
})