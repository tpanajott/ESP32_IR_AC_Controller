connected = true;

function updateValues() {
    // Get all AC status data
    $.getJSON("/status", function(data) {
        $.each(data, function(key, val) {
            $('#' + key).html(val);
        });

        if(data["ac_learning"] == true) {
            // 0 = Learning is inactive.
            $("#learn_from_remote_modal").removeClass("is-active");
        } else if (data == 1) {
            // 1 = Learning is active.
            $("#learn_from_remote_modal").addClass("is-active");
        }

        $("#mqtt_status_tag").attr("class", data["mqtt_status_tag_classes"]);
        $("#wifi_status_tag").attr("class", data["wifi_status_tag_classes"]);

        // We managed to get a response from the ESP. Connection is ok.
        $("#device_connection_status_tag").attr("class", "tag is-success");
        $("#device_connection_status_tag").html("Connected");
        $(".button").prop("disabled", false);
    }).fail(function() {
        $("#device_connection_status_tag").attr("class", "tag is-danger");
        $("#device_connection_status_tag").html("FAILED");

        $("#mqtt_status_tag").attr("class", "tag is-light");
        $("#mqtt_status_tag").html("MQTT: UNKNOWN");

        $("#wifi_status_tag").attr("class", "tag is-light");
        $("#wifi_status_tag").html("WiFi: UNKNOWN");
        $(".button").prop("disabled", true);
    });
}

function learnFromRemote() {
    $.get("/learn/start").done(function() {
        updateValues();
    }).fail(function() {
        alert("Failed to start learning!");
    });
}

function stopLearningFromRemote() {
    $.get("/learn/stop").done(function() {
        updateValues();
    }).fail(function() {
        alert("Failed to stop learning!");
    });
}

function updateStatus() {
    setTimeout(function() {
        updateValues(); // Update all values
        updateStatus(); // Run this function again ie. check state again in 1000ms.
    }, 1000);
}

function act(action, value) {
    var data = {};
    data[action] = value;
    $.get("/set", data).done(function() {
        updateValues();
    }).fail(function() {
        console.log("Action failed!");
    });
}

jQuery(document).ready(function(){  
    $.ajaxSetup({timeout:1000});
    updateValues(); // Pull values as soon as web page is ready.
    updateStatus(); // Start process to pull values every 1000ms.
});