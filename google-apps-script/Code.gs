function doPost(e) {
  try {
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();

    // Debug: raw incoming body
    var rawBody = "";
    if (e && e.postData && e.postData.contents) {
      rawBody = e.postData.contents;
    } else {
      throw new Error("No POST data received");
    }

    var data = JSON.parse(rawBody);

    // Safe numeric values
    var roomCapacity = Number(data.roomCapacity || 0);
    var currentCount = Number(data.currentCount || 0);
    var inCount = Number(data.inCount || 0);
    var outCount = Number(data.outCount || 0);
    var occupancyPercent = Number(data.occupancyPercent || 0);
    var availableSpace = Number(data.availableSpace || 0);

    sheet.appendRow([
      new Date(),                       // Google server timestamp
      data.deviceId || "",
      data.roomName || "",
      Number(data.gateType || 0),
      roomCapacity,
      currentCount,
      inCount,
      outCount,
      data.gate1Status || "",
      data.gate2Status || "",
      data.deviceStatus || "",
      Number(data.lastUpdateMs || 0),
      occupancyPercent,
      availableSpace
    ]);

    return ContentService
      .createTextOutput(JSON.stringify({
        status: "success",
        message: "Row added successfully",
        received: data
      }))
      .setMimeType(ContentService.MimeType.JSON);

  } catch (error) {
    return ContentService
      .createTextOutput(JSON.stringify({
        status: "error",
        message: error.toString(),
        rawEvent: e ? JSON.stringify(e) : "No event object"
      }))
      .setMimeType(ContentService.MimeType.JSON);
  }
}
function doGet(e) {
  const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  const data = sheet.getDataRange().getValues();

  data.shift(); // remove header

  const deviceId = e.parameter.deviceId;

  let filtered = [];

  data.forEach((row) => {
    if (!deviceId || row[1] === deviceId) {
      filtered.push({
        time: row[0],
        count: Number(row[5]),
        deviceId: row[1]
      });
    }
  });

  //  SORT BY TIME (latest first)
  filtered.sort((a, b) => new Date(b.time) - new Date(a.time));

  //  TAKE ONLY 20
  filtered = filtered.slice(0, 20);

  // OPTIONAL: reverse for graph (old → new)
  filtered.reverse();

  return ContentService
    .createTextOutput(JSON.stringify(filtered))
    .setMimeType(ContentService.MimeType.JSON);
}