/*
 * xohtml's inline tooltip.  Shows the data-tag/data-type/data-help/
 * data-xpath attributes libxo stamps on each "data" div when the
 * "info" and/or "xpath" libxo options are used.  Plain DOM, no
 * external libraries.
 */
(function () {
    "use strict";

    var tip = null;
    var hideTimer = null;

    function cancelHide () {
        if (hideTimer) {
            clearTimeout(hideTimer);
            hideTimer = null;
        }
    }

    function scheduleHide () {
        cancelHide();
        hideTimer = setTimeout(function () {
            if (tip)
                tip.style.display = "none";
        }, 300);
    }

    function ensureTip () {
        if (tip)
            return tip;

        tip = document.createElement("div");
        tip.className = "xohtml-tip";
        tip.style.display = "none";
        tip.addEventListener("mouseenter", cancelHide);
        tip.addEventListener("mouseleave", scheduleHide);
        document.body.appendChild(tip);
        return tip;
    }

    function addLabeled (parent, label, text) {
        var div = document.createElement("div");
        var b = document.createElement("b");
        b.textContent = label + ": ";
        div.appendChild(b);
        div.appendChild(document.createTextNode(text));
        parent.appendChild(div);
    }

    function buildContent (el) {
        var help = el.getAttribute("data-help");
        var type = el.getAttribute("data-type");
        var units = el.getAttribute("data-units");
        var xpath = el.getAttribute("data-xpath");
        var tag = el.getAttribute("data-tag");

        var frag = document.createDocumentFragment();

        if (tag) {
            var title = document.createElement("div");
            var b = document.createElement("b");
            b.textContent = tag;
            title.appendChild(b);
            frag.appendChild(title);
        }

        if (help)
            addLabeled(frag, "Help", help);
        if (type)
            addLabeled(frag, "Type", type);
        if (units)
            addLabeled(frag, "Units", units);

        if (xpath) {
            var wrap = document.createElement("div");
            wrap.className = "xpath-wrapper";

            var link = document.createElement("a");
            link.href = "#";
            link.className = "xpath-link";
            link.textContent = "show xpath";

            var box = document.createElement("div");
            box.className = "xpath";
            box.textContent = xpath;
            box.style.display = "none";

            link.addEventListener("click", function (ev) {
                ev.preventDefault();
                var hidden = box.style.display === "none";
                box.style.display = hidden ? "" : "none";
                link.textContent = hidden ? "hide xpath" : "show xpath";
            });

            wrap.appendChild(link);
            wrap.appendChild(box);
            frag.appendChild(wrap);
        }

        return frag;
    }

    function showTip (el) {
        var hasContent = el.getAttribute("data-help")
            || el.getAttribute("data-type")
            || el.getAttribute("data-units")
            || el.getAttribute("data-xpath")
            || el.getAttribute("data-tag");
        if (!hasContent)
            return;

        cancelHide();

        var t = ensureTip();
        t.innerHTML = "";
        t.appendChild(buildContent(el));
        t.style.display = "block";

        var rect = el.getBoundingClientRect();
        var top = rect.bottom + window.scrollY + 4;
        var left = rect.left + window.scrollX;

        /* Keep the tip from running off the right edge of the window. */
        var maxLeft = window.scrollX + document.documentElement.clientWidth
            - t.offsetWidth - 4;
        if (maxLeft > 0 && left > maxLeft)
            left = maxLeft;

        t.style.top = top + "px";
        t.style.left = left + "px";
    }

    function wireTooltips () {
        var els = document.querySelectorAll("#xohtml-content .data[data-tag]");
        for (var i = 0; i < els.length; i++) {
            els[i].addEventListener("mouseenter", (function (el) {
                return function () { showTip(el); };
            })(els[i]));
            els[i].addEventListener("mouseleave", scheduleHide);
        }
    }

    /*
     * Sortable table view.  libxo wraps every output line in
     * <div class="line">...</div> (xo_line_ensure_open), and a line
     * is a header/title row when every one of its child divs has
     * class "title" (xo_buf_append_div).  We use that to find
     * header+row groups and build an optional <table> rendering,
     * toggled on top of the normal line-by-line flow.
     */

    function isTitleLine (line) {
        var kids = line.children;
        if (!kids.length)
            return false;
        for (var i = 0; i < kids.length; i++)
            if (!kids[i].classList.contains("title"))
                return false;
        return true;
    }

    function findGroups (content) {
        var lines = content.querySelectorAll("div.line");
        var groups = [];
        var current = null;

        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            if (isTitleLine(line)) {
                current = { header: line, rows: [] };
                groups.push(current);
            } else if (!line.children.length) {
                /* A blank line ends the current table. */
                current = null;
            } else if (current) {
                current.rows.push(line);
            }
        }

        return groups;
    }

    /*
     * Untitled record dumps ("Item 'gum': / Total sold: ... / SKU: ...")
     * have no title line to key off of, but libxo stamps every field
     * belonging to the same list/instance with a shared, monotonically
     * increasing data-ident (xo_depth_change in libxo.c).  Opening a new
     * list or instance always burns an id that never appears on a field,
     * so unrelated lists never end up with contiguous idents even when
     * their field shapes are identical -- a run of contiguous idents is
     * exactly one table.
     */

    function rowIdent (line) {
        var el = line.querySelector(".data[data-tag][data-ident]");
        return el ? el.getAttribute("data-ident") : null;
    }

    function humanizeTag (tag) {
        return tag.split("-").map(function (word) {
            return word.charAt(0).toUpperCase() + word.slice(1);
        }).join(" ");
    }

    function findIdentGroups (content, used) {
        var lines = content.querySelectorAll("div.line");
        var groups = [];

        var rows = [];
        var curIdent = null;
        var curLines = [];

        function finishRow () {
            if (curLines.length)
                rows.push(curLines);
            curLines = [];
        }

        function finishGroup () {
            finishRow();
            if (rows.length >= 2)
                groups.push({ rows: rows });
            rows = [];
            curIdent = null;
        }

        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            var ident = used.has(line) ? null : rowIdent(line);

            if (ident === null) {
                finishGroup();
                continue;
            }

            if (curIdent === null) {
                curIdent = ident;
                curLines = [line];
            } else if (ident === curIdent) {
                curLines.push(line);
            } else if (Number(ident) === Number(curIdent) + 1) {
                finishRow();
                curIdent = ident;
                curLines = [line];
            } else {
                /* Not contiguous with the row just finished: new table. */
                finishGroup();
                curIdent = ident;
                curLines = [line];
            }
        }
        finishGroup();

        return groups;
    }

    function buildModel (group) {
        var columns = [];
        var seen = {};
        var rows = [];

        for (var r = 0; r < group.rows.length; r++) {
            var cells = group.rows[r].querySelectorAll(".data[data-tag]");
            if (!cells.length)
                continue;

            var rowData = {};
            for (var c = 0; c < cells.length; c++) {
                var tag = cells[c].getAttribute("data-tag");
                if (!(tag in seen)) {
                    seen[tag] = true;
                    columns.push(tag);
                }
                rowData[tag] = cells[c].textContent.trim();
            }
            rows.push(rowData);
        }

        if (!columns.length || !rows.length)
            return null;

        var titleDivs = group.header.querySelectorAll(".title");
        var labels = columns.map(function (tag, i) {
            var text = titleDivs[i] ? titleDivs[i].textContent : tag;
            return text.trim() || tag;
        });

        return { columns: columns, labels: labels, rows: rows };
    }

    function buildIdentModel (group) {
        var columns = [];
        var labelMap = {};
        var rows = [];

        group.rows.forEach(function (rowLines) {
            var rowData = {};
            var any = false;

            rowLines.forEach(function (line) {
                var cells = line.querySelectorAll(".data[data-tag]");
                var label = line.querySelector(".label");
                var labelText = label ? label.textContent.trim() : null;

                for (var c = 0; c < cells.length; c++) {
                    var tag = cells[c].getAttribute("data-tag");
                    if (!(tag in labelMap)) {
                        labelMap[tag] = labelText || humanizeTag(tag);
                        columns.push(tag);
                    }
                    rowData[tag] = cells[c].textContent.trim();
                    any = true;
                }
            });

            if (any)
                rows.push(rowData);
        });

        if (!columns.length || rows.length < 2)
            return null;

        var labels = columns.map(function (tag) {
            return labelMap[tag];
        });

        return { columns: columns, labels: labels, rows: rows };
    }

    function compareValues (a, b) {
        var na = parseFloat(a);
        var nb = parseFloat(b);
        if (!isNaN(na) && !isNaN(nb))
            return na - nb;
        return String(a).localeCompare(String(b));
    }

    function buildTable (model) {
        var table = document.createElement("table");
        table.className = "xohtml-table";

        var sortCol = -1;
        var sortDir = 1;

        var thead = document.createElement("thead");
        var headRow = document.createElement("tr");
        var ths = [];

        model.columns.forEach(function (tag, i) {
            var th = document.createElement("th");
            th.appendChild(document.createTextNode(model.labels[i]));

            var arrow = document.createElement("span");
            arrow.className = "xohtml-sort-arrow";
            th.appendChild(arrow);

            th.addEventListener("click", function () {
                sortDir = (sortCol === i) ? -sortDir : 1;
                sortCol = i;

                model.rows.sort(function (ra, rb) {
                    return sortDir * compareValues(ra[tag], rb[tag]);
                });

                ths.forEach(function (other, oi) {
                    other.querySelector(".xohtml-sort-arrow").textContent =
                        (oi === i) ? (sortDir > 0 ? "▲" : "▼") : "";
                });

                renderBody();
            });

            ths.push(th);
            headRow.appendChild(th);
        });

        thead.appendChild(headRow);
        table.appendChild(thead);

        var tbody = document.createElement("tbody");
        table.appendChild(tbody);

        function renderBody () {
            tbody.innerHTML = "";
            model.rows.forEach(function (rowData) {
                var tr = document.createElement("tr");
                model.columns.forEach(function (tag) {
                    var td = document.createElement("td");
                    td.textContent = rowData[tag] || "";
                    tr.appendChild(td);
                });
                tbody.appendChild(tr);
            });
        }

        renderBody();

        return table;
    }

    function addToggle (table, lines, anchor, place) {
        var toggle = document.createElement("button");
        toggle.type = "button";
        toggle.className = "xohtml-view-toggle";
        toggle.textContent = "Table view";

        toggle.addEventListener("click", function () {
            var on = table.style.display === "none";
            table.style.display = on ? "table" : "none";
            lines.forEach(function (line) {
                line.style.display = on ? "none" : "";
            });
            toggle.textContent = on ? "Line view" : "Table view";
        });

        if (place === "append")
            anchor.appendChild(toggle);
        else
            anchor.insertAdjacentElement(place, toggle);

        return toggle;
    }

    function setupTitledGroups (content, used) {
        var groups = findGroups(content);

        groups.forEach(function (group) {
            used.add(group.header);
            group.rows.forEach(function (row) { used.add(row); });

            var model = buildModel(group);
            if (!model)
                return;

            var table = buildTable(model);
            table.style.display = "none";
            group.header.insertAdjacentElement("afterend", table);

            addToggle(table, group.rows, group.header, "append");
        });
    }

    function setupIdentGroups (content, used) {
        var groups = findIdentGroups(content, used);

        groups.forEach(function (group) {
            var model = buildIdentModel(group);
            if (!model)
                return;

            var allLines = [];
            group.rows.forEach(function (rowLines) {
                rowLines.forEach(function (line) {
                    allLines.push(line);
                });
            });

            var table = buildTable(model);
            table.style.display = "none";
            allLines[allLines.length - 1]
                .insertAdjacentElement("afterend", table);

            var bar = document.createElement("div");
            bar.className = "line xohtml-ident-toggle";
            allLines[0].insertAdjacentElement("beforebegin", bar);

            addToggle(table, allLines, bar, "append");
        });
    }

    function setupTableView () {
        var content = document.getElementById("xohtml-content");
        if (!content)
            return;

        var used = new Set();
        setupTitledGroups(content, used);
        setupIdentGroups(content, used);
    }

    function init () {
        wireTooltips();
        setupTableView();
    }

    if (document.readyState === "loading")
        document.addEventListener("DOMContentLoaded", init);
    else
        init();
})();
