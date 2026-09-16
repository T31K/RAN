


-- viewLogItemExchangeMax
CREATE VIEW [dbo].[viewLogItemExchangeMax]
AS
SELECT   TOP 100 PERCENT MAX(MakeNum) AS MaxNum, NIDMain, NIDSub, MakeType, 
                SGNum, SvrNum, FldNum
FROM      dbo.LogItemExchange
GROUP BY SGNum, SvrNum, FldNum, MakeType, NIDMain, NIDSub
ORDER BY SGNum, SvrNum, FldNum, MakeType, NIDMain, NIDSub, MaxNum




