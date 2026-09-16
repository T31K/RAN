


-- viewLogItemMax
CREATE VIEW [dbo].[viewLogItemMax]
AS
SELECT TOP 100 PERCENT dbo.LogItemMax.NIDMain, dbo.LogItemMax.NIDSub, 
       dbo.LogItemMax.SGNum, dbo.LogItemMax.SvrNum, dbo.LogItemMax.FldNum, 
       dbo.LogItemMax.MakeType, dbo.LogItemMax.MaxNum, 
       dbo.ItemList.ItemName
FROM   dbo.LogItemMax 
       INNER JOIN dbo.ItemList ON 
       dbo.ItemList.ItemMain = dbo.LogItemMax.NIDMain AND dbo.ItemList.ItemSub = dbo.LogItemMax.NIDSub
ORDER BY dbo.LogItemMax.MaxNum DESC




